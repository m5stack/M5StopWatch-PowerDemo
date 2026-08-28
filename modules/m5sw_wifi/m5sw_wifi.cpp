#include "m5sw_wifi.hpp"

#include <atomic>
#include <cstring>
#include <new>

#include "apps/ping/ping_sock.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace m5sw::wifi {
namespace {

constexpr char kTag[] = "m5sw_wifi";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr std::uint8_t kConnectionRetryCount = 3;

WifiResult result(esp_err_t error)
{
    return {error, static_cast<int>(error)};
}

wifi_ps_type_t native_power_save(PowerSave mode)
{
    switch (mode) {
        case PowerSave::none:
            return WIFI_PS_NONE;
        case PowerSave::min_modem:
            return WIFI_PS_MIN_MODEM;
        case PowerSave::max_modem:
            return WIFI_PS_MAX_MODEM;
    }
    return WIFI_PS_NONE;
}

}  // namespace

class WifiController::Impl final {
public:
    ~Impl() { (void)deinitialize(); }

    WifiResult begin(PowerSave initial_power_save)
    {
        if (initialized_.load()) return set_power_save(initial_power_save);

        event_group_ = xEventGroupCreate();
        if (event_group_ == nullptr) return result(ESP_ERR_NO_MEM);
        if (ping_done_ == nullptr) ping_done_ = xSemaphoreCreateBinary();
        if (ping_done_ == nullptr) return rollback(ESP_ERR_NO_MEM);

        esp_err_t error = nvs_flash_init();
        if (error != ESP_OK) return rollback(error);
        error = esp_netif_init();
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return rollback(error);
        error = esp_event_loop_create_default();
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return rollback(error);

        station_netif_ = esp_netif_create_default_wifi_sta();
        if (station_netif_ == nullptr) return rollback(ESP_ERR_NO_MEM);

        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        error = esp_wifi_init(&init_config);
        if (error != ESP_OK) return rollback(error);
        wifi_initialized_ = true;

        error = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this, &wifi_handler_);
        if (error != ESP_OK) return rollback(error);
        wifi_handler_registered_ = true;
        error = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this, &ip_handler_);
        if (error != ESP_OK) return rollback(error);
        ip_handler_registered_ = true;

        error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (error != ESP_OK) return rollback(error);
        error = esp_wifi_set_mode(WIFI_MODE_STA);
        if (error != ESP_OK) return rollback(error);
        error = esp_wifi_start();
        if (error != ESP_OK) return rollback(error);
        wifi_started_ = true;
        state_.store(State::disconnected);
        initialized_.store(true);

        const WifiResult ps_result = set_power_save(initial_power_save);
        if (!ps_result.ok()) return rollback(ps_result.error);
        return {};
    }

    WifiResult connect(std::uint32_t timeout_ms)
    {
        if (!initialized_.load()) return result(ESP_ERR_INVALID_STATE);
        if (!credentials_configured() || timeout_ms == 0U) return result(ESP_ERR_INVALID_ARG);
        if (state_.load() == State::connected) return {};

        wifi_config_t config = {};
        const std::size_t ssid_length = std::strlen(CONFIG_M5SW_WIFI_SSID);
        const std::size_t password_length = std::strlen(CONFIG_M5SW_WIFI_PASSWORD);
        if (ssid_length >= sizeof(config.sta.ssid) ||
            password_length >= sizeof(config.sta.password)) {
            return result(ESP_ERR_INVALID_SIZE);
        }
        std::memcpy(config.sta.ssid, CONFIG_M5SW_WIFI_SSID, ssid_length);
        std::memcpy(config.sta.password, CONFIG_M5SW_WIFI_PASSWORD, password_length);
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        config.sta.listen_interval = CONFIG_M5SW_WIFI_LISTEN_INTERVAL;
        config.sta.threshold.rssi = -127;
        config.sta.threshold.authmode =
            password_length == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        config.sta.failure_retry_cnt = kConnectionRetryCount;

        esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (error != ESP_OK) return result(error);
        xEventGroupClearBits(event_group_, kConnectedBit);
        reconnect_enabled_.store(true);
        state_.store(State::connecting);
        error = esp_wifi_connect();
        if (error != ESP_OK) {
            reconnect_enabled_.store(false);
            state_.store(State::disconnected);
            return result(error);
        }

        const EventBits_t bits = xEventGroupWaitBits(
            event_group_, kConnectedBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
        return (bits & kConnectedBit) != 0U ? WifiResult{} : result(ESP_ERR_TIMEOUT);
    }

    WifiResult disconnect()
    {
        if (!initialized_.load()) return result(ESP_ERR_INVALID_STATE);
        reconnect_enabled_.store(false);
        const esp_err_t error = esp_wifi_disconnect();
        xEventGroupClearBits(event_group_, kConnectedBit);
        state_.store(State::disconnected);
        return result(error);
    }

    WifiResult set_power_save(PowerSave mode)
    {
        if (!initialized_.load()) return result(ESP_ERR_INVALID_STATE);
        const wifi_ps_type_t expected = native_power_save(mode);
        esp_err_t error = esp_wifi_set_ps(expected);
        if (error != ESP_OK) return result(error);
        wifi_ps_type_t actual = WIFI_PS_MIN_MODEM;
        error = esp_wifi_get_ps(&actual);
        if (error != ESP_OK) return result(error);
        if (actual != expected) return result(ESP_ERR_INVALID_RESPONSE);
        power_save_.store(mode);
        return {};
    }

    WifiResult scan(std::uint16_t* access_point_count)
    {
        if (!initialized_.load() || access_point_count == nullptr) {
            return result(access_point_count == nullptr ? ESP_ERR_INVALID_ARG
                                                        : ESP_ERR_INVALID_STATE);
        }
        if (state_.load() != State::connected) return result(ESP_ERR_INVALID_STATE);

        wifi_scan_config_t config = {};
        config.show_hidden = false;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        esp_err_t error = esp_wifi_scan_start(&config, true);
        if (error != ESP_OK) return result(error);
        error = esp_wifi_scan_get_ap_num(access_point_count);
        const esp_err_t clear_error = esp_wifi_clear_ap_list();
        if (error != ESP_OK) return result(error);
        return result(clear_error);
    }

    WifiResult ping_once(std::uint32_t timeout_ms)
    {
        if (!initialized_.load() || state_.load() != State::connected) {
            return result(ESP_ERR_INVALID_STATE);
        }
        if (!target_configured() || timeout_ms == 0U) return result(ESP_ERR_INVALID_ARG);
        bool expected = false;
        if (!ping_active_.compare_exchange_strong(expected, true)) {
            return result(ESP_ERR_INVALID_STATE);
        }

        ip_addr_t target = {};
        if (ipaddr_aton(CONFIG_M5SW_WIFI_TARGET_IPV4, &target) == 0 || !IP_IS_V4(&target)) {
            ping_active_.store(false);
            return result(ESP_ERR_INVALID_ARG);
        }
        xSemaphoreTake(ping_done_, 0);
        last_ping_succeeded_.store(false);

        esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
        config.count = 1;
        config.interval_ms = 0;
        config.timeout_ms = timeout_ms;
        config.data_size = kPeriodicTrafficPayloadBytes;
        config.target_addr = target;
        esp_ping_callbacks_t callbacks = {};
        callbacks.cb_args = this;
        callbacks.on_ping_success = &on_ping_success;
        callbacks.on_ping_timeout = &on_ping_timeout;
        callbacks.on_ping_end = &on_ping_end;

        esp_ping_handle_t handle = nullptr;
        esp_err_t error = esp_ping_new_session(&config, &callbacks, &handle);
        if (error != ESP_OK) {
            ping_active_.store(false);
            return result(error);
        }
        error = esp_ping_start(handle);
        if (error != ESP_OK) {
            (void)esp_ping_delete_session(handle);
            ping_active_.store(false);
            return result(error);
        }

        const BaseType_t completed = xSemaphoreTake(
            ping_done_, pdMS_TO_TICKS(timeout_ms + 500U));
        if (completed != pdTRUE) {
            (void)esp_ping_stop(handle);
            // The ping task is still inside its bounded receive call. Wait for
            // on_ping_end so no callback can outlive this operation.
            xSemaphoreTake(ping_done_, portMAX_DELAY);
            return result(ESP_ERR_TIMEOUT);
        }
        return last_ping_succeeded_.load() ? WifiResult{} : result(ESP_ERR_TIMEOUT);
    }

    WifiResult network_info(NetworkInfo* info) const
    {
        if (!initialized_.load() || info == nullptr) {
            return result(info == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE);
        }
        if (state_.load() != State::connected) return result(ESP_ERR_INVALID_STATE);

        wifi_ap_record_t record = {};
        esp_err_t error = esp_wifi_sta_get_ap_info(&record);
        if (error != ESP_OK) return result(error);
        wifi_config_t station_config = {};
        error = esp_wifi_get_config(WIFI_IF_STA, &station_config);
        if (error != ESP_OK) return result(error);
        esp_netif_ip_info_t ip_info = {};
        error = esp_netif_get_ip_info(station_netif_, &ip_info);
        if (error != ESP_OK) return result(error);
        info->rssi_dbm = record.rssi;
        info->channel = record.primary;
        info->listen_interval = station_config.sta.listen_interval;
        info->ipv4_address = ip_info.ip.addr;
        return {};
    }

    WifiResult deinitialize()
    {
        reconnect_enabled_.store(false);
        initialized_.store(false);
        state_.store(State::stopped);
        esp_err_t first_error = ESP_OK;

        if (wifi_started_) {
            (void)esp_wifi_disconnect();
            const esp_err_t error = esp_wifi_stop();
            if (first_error == ESP_OK && error != ESP_OK) first_error = error;
            wifi_started_ = false;
        }
        if (ip_handler_registered_) {
            const esp_err_t error = esp_event_handler_instance_unregister(
                IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_);
            if (first_error == ESP_OK && error != ESP_OK) first_error = error;
            ip_handler_registered_ = false;
        }
        if (wifi_handler_registered_) {
            const esp_err_t error = esp_event_handler_instance_unregister(
                WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
            if (first_error == ESP_OK && error != ESP_OK) first_error = error;
            wifi_handler_registered_ = false;
        }
        if (wifi_initialized_) {
            const esp_err_t error = esp_wifi_deinit();
            if (first_error == ESP_OK && error != ESP_OK) first_error = error;
            wifi_initialized_ = false;
        }
        if (station_netif_ != nullptr) {
            esp_netif_destroy_default_wifi(station_netif_);
            station_netif_ = nullptr;
        }
        if (event_group_ != nullptr) {
            vEventGroupDelete(event_group_);
            event_group_ = nullptr;
        }
        if (ping_done_ != nullptr && !ping_active_.load()) {
            vSemaphoreDelete(ping_done_);
            ping_done_ = nullptr;
        }
        return result(first_error);
    }

    bool credentials_configured() const { return CONFIG_M5SW_WIFI_SSID[0] != '\0'; }
    bool target_configured() const { return CONFIG_M5SW_WIFI_TARGET_IPV4[0] != '\0'; }
    bool initialized() const { return initialized_.load(); }
    State state() const { return state_.load(); }
    PowerSave power_save() const { return power_save_.load(); }

    TrafficStats traffic_stats() const
    {
        return {
            ping_attempts_.load(),
            ping_replies_.load(),
            ping_timeouts_.load(),
            static_cast<std::uint64_t>(total_rtt_ms_.load()),
        };
    }

private:
    WifiResult rollback(esp_err_t error)
    {
        (void)deinitialize();
        return result(error);
    }

    static void event_handler(void* argument, esp_event_base_t base,
                              std::int32_t event_id, void* event_data)
    {
        auto* self = static_cast<Impl*>(argument);
        if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
            xEventGroupClearBits(self->event_group_, kConnectedBit);
            self->state_.store(State::disconnected);
            ESP_LOGW(kTag, "STA disconnected reason=%u",
                     static_cast<unsigned>(event->reason));
            if (self->reconnect_enabled_.load() && self->initialized_.load()) {
                self->state_.store(State::connecting);
                const esp_err_t error = esp_wifi_connect();
                if (error != ESP_OK) {
                    self->state_.store(State::disconnected);
                    ESP_LOGE(kTag, "STA reconnect request failed: %s",
                             esp_err_to_name(error));
                }
            }
        } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            auto* event = static_cast<ip_event_got_ip_t*>(event_data);
            self->state_.store(State::connected);
            xEventGroupSetBits(self->event_group_, kConnectedBit);
            ESP_LOGI(kTag, "STA connected ip=" IPSTR, IP2STR(&event->ip_info.ip));
        }
    }

    static void on_ping_success(esp_ping_handle_t handle, void* argument)
    {
        auto* self = static_cast<Impl*>(argument);
        std::uint32_t rtt_ms = 0;
        (void)esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP,
                                   &rtt_ms, sizeof(rtt_ms));
        self->ping_attempts_.fetch_add(1);
        self->ping_replies_.fetch_add(1);
        self->total_rtt_ms_.fetch_add(rtt_ms);
        self->last_ping_succeeded_.store(true);
    }

    static void on_ping_timeout(esp_ping_handle_t, void* argument)
    {
        auto* self = static_cast<Impl*>(argument);
        self->ping_attempts_.fetch_add(1);
        self->ping_timeouts_.fetch_add(1);
        self->last_ping_succeeded_.store(false);
    }

    static void on_ping_end(esp_ping_handle_t handle, void* argument)
    {
        auto* self = static_cast<Impl*>(argument);
        (void)esp_ping_delete_session(handle);
        self->ping_active_.store(false);
        xSemaphoreGive(self->ping_done_);
    }

    EventGroupHandle_t event_group_ = nullptr;
    SemaphoreHandle_t ping_done_ = nullptr;
    esp_netif_t* station_netif_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr;
    esp_event_handler_instance_t ip_handler_ = nullptr;
    bool wifi_initialized_ = false;
    bool wifi_started_ = false;
    bool wifi_handler_registered_ = false;
    bool ip_handler_registered_ = false;
    std::atomic_bool initialized_{false};
    std::atomic_bool reconnect_enabled_{false};
    std::atomic_bool ping_active_{false};
    std::atomic_bool last_ping_succeeded_{false};
    std::atomic<State> state_{State::stopped};
    std::atomic<PowerSave> power_save_{PowerSave::none};
    std::atomic_uint32_t ping_attempts_{0};
    std::atomic_uint32_t ping_replies_{0};
    std::atomic_uint32_t ping_timeouts_{0};
    std::atomic_uint32_t total_rtt_ms_{0};
};

WifiController::~WifiController()
{
    delete impl_;
}

WifiResult WifiController::begin(PowerSave initial_power_save)
{
    if (impl_ == nullptr) {
        impl_ = new (std::nothrow) Impl();
        if (impl_ == nullptr) return result(ESP_ERR_NO_MEM);
    }
    return impl_->begin(initial_power_save);
}

WifiResult WifiController::connect(std::uint32_t timeout_ms)
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->connect(timeout_ms);
}

WifiResult WifiController::disconnect()
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->disconnect();
}

WifiResult WifiController::set_power_save(PowerSave mode)
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->set_power_save(mode);
}

WifiResult WifiController::scan(std::uint16_t* access_point_count)
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->scan(access_point_count);
}

WifiResult WifiController::ping_once(std::uint32_t timeout_ms)
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->ping_once(timeout_ms);
}

WifiResult WifiController::network_info(NetworkInfo* info) const
{
    return impl_ == nullptr ? result(ESP_ERR_INVALID_STATE) : impl_->network_info(info);
}

WifiResult WifiController::deinitialize()
{
    return impl_ == nullptr ? WifiResult{} : impl_->deinitialize();
}

bool WifiController::initialized() const
{
    return impl_ != nullptr && impl_->initialized();
}

bool WifiController::credentials_configured() const
{
    return impl_ != nullptr ? impl_->credentials_configured()
                            : CONFIG_M5SW_WIFI_SSID[0] != '\0';
}

bool WifiController::target_configured() const
{
    return impl_ != nullptr ? impl_->target_configured()
                            : CONFIG_M5SW_WIFI_TARGET_IPV4[0] != '\0';
}

State WifiController::state() const
{
    return impl_ == nullptr ? State::stopped : impl_->state();
}

PowerSave WifiController::power_save() const
{
    return impl_ == nullptr ? PowerSave::none : impl_->power_save();
}

TrafficStats WifiController::traffic_stats() const
{
    return impl_ == nullptr ? TrafficStats{} : impl_->traffic_stats();
}

}  // namespace m5sw::wifi
