#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_test_support.hpp"
#include "m5sw_wifi.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[]                             = "wifi_periodic_traffic";
constexpr std::uint32_t kConnectTimeoutMs         = 15000;
constexpr std::uint32_t kPowerOnSettleMs          = 1000;
constexpr std::uint32_t kPingTimeoutMs            = 500;
constexpr std::uint32_t kPreflightAttempts        = 10;
constexpr std::uint32_t kMinimumSuccessPercent    = 90;
constexpr std::uint16_t kExpectedBeaconIntervalTu = 100;
constexpr std::uint8_t kExpectedDtimPeriod        = 3;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240,
              "Wi-Fi periodic traffic requires the fixed 240 MHz CPU configuration");
static_assert(m5sw::wifi::kPeriodicTrafficIntervalMs == 1000, "Wi-Fi periodic traffic requires a one-second period");
static_assert(m5sw::wifi::kPeriodicTrafficPayloadBytes == 64, "Wi-Fi periodic traffic requires a 64-byte ICMP payload");

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage, esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "Wi-Fi periodic traffic setup failed");
    m5sw::test_support::hold_safe_idle();
}

void require(const char* stage, esp_err_t error)
{
    if (error != ESP_OK) fail(stage, error, error);
}

void require(const char* stage, m5sw::wifi::WifiResult result)
{
    if (!result.ok()) fail(stage, result.error, result.raw_error);
}

void require(const char* stage, m5sw::power::PowerResult result)
{
    if (!result.ok()) fail(stage, result.error, result.raw_error);
}

m5sw::wifi::TrafficStats run_preflight(m5sw::wifi::WifiController* wifi)
{
    TickType_t last_wake = xTaskGetTickCount();
    for (std::uint32_t attempt = 0; attempt < kPreflightAttempts; ++attempt) {
        const m5sw::wifi::WifiResult ping_result = wifi->ping_once(kPingTimeoutMs);
        if (!ping_result.ok() && ping_result.error != ESP_ERR_TIMEOUT) {
            fail("ping_preflight", ping_result.error, ping_result.raw_error);
        }
        if (attempt + 1U < kPreflightAttempts) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(m5sw::wifi::kPeriodicTrafficIntervalMs));
        }
    }

    const m5sw::wifi::TrafficStats stats = wifi->traffic_stats();
    if (stats.attempts != kPreflightAttempts) {
        fail("ping_preflight_count", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
    const std::uint32_t success_percent = stats.replies * 100U / stats.attempts;
    if (success_percent < kMinimumSuccessPercent) {
        fail("ping_preflight_rate", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
    return stats;
}

[[noreturn]] void run_periodic_traffic(m5sw::wifi::WifiController* wifi)
{
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        const m5sw::wifi::WifiResult result = wifi->ping_once(kPingTimeoutMs);
        if (!result.ok() && result.error != ESP_ERR_TIMEOUT) {
            fail("ping_runtime", result.error, result.raw_error);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(m5sw::wifi::kPeriodicTrafficIntervalMs));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    require("test_support", m5sw::test_support::initialize(&context));

    static m5sw::wifi::WifiController wifi;
    if (!wifi.credentials_configured()) {
        fail("wifi_credentials", ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE);
    }
    if (!wifi.target_configured()) {
        fail("wifi_target", ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE);
    }

    gpio_deep_sleep_hold_dis();
    vTaskDelay(pdMS_TO_TICKS(kPowerOnSettleMs));
    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);
    require("i2c", m5sw::board::initialize_i2c(m5sw::board::I2cSpeed::standard));
    i2c_bus_handle_t bus = nullptr;
    require("i2c_claim", m5sw::board::acquire_i2c(&bus));

    static m5sw::power::PowerController power;
    require("l2_power_begin", power.begin(bus));
    require("l2_active_prepare", power.prepare_l2_active());

    require("wifi_begin", wifi.begin());
    require("wifi_ps_none", wifi.set_power_save(m5sw::wifi::PowerSave::none));
    require("wifi_connect", wifi.connect(kConnectTimeoutMs));
    if (wifi.state() != m5sw::wifi::State::connected || wifi.power_save() != m5sw::wifi::PowerSave::none) {
        fail("wifi_state", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }

    m5sw::wifi::NetworkInfo info = {};
    require("wifi_info", wifi.network_info(&info));
    if (info.channel == 0U || info.channel > 14U || info.ipv4_address == 0U) {
        fail("wifi_network", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }

    const m5sw::wifi::TrafficStats preflight = run_preflight(&wifi);
    const std::uint32_t success_percent      = preflight.replies * 100U / preflight.attempts;
    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 BASELINE=l2-active WIFI=connected PS=none TARGET=%s CHANNEL=%u RSSI_DBM=%d "
             "BEACON_TU_EXPECTED=%u DTIM_EXPECTED=%u PERIOD_MS=%u PAYLOAD_BYTES=%u TIMEOUT_MS=%u PREFLIGHT_ATTEMPTS=%u "
             "PREFLIGHT_REPLIES=%u PREFLIGHT_SUCCESS_PCT=%u MIN_SUCCESS_PCT=%u SCAN_AFTER_CONNECT=0 L1=on DISPLAY=0 "
             "TP=sleep IMU=sleep RTC=retained AUDIO=0 MOTOR=0 EXT5V=0",
             CONFIG_M5SW_WIFI_TARGET_IPV4, static_cast<unsigned>(info.channel), static_cast<int>(info.rssi_dbm),
             static_cast<unsigned>(kExpectedBeaconIntervalTu), static_cast<unsigned>(kExpectedDtimPeriod),
             static_cast<unsigned>(m5sw::wifi::kPeriodicTrafficIntervalMs),
             static_cast<unsigned>(m5sw::wifi::kPeriodicTrafficPayloadBytes), static_cast<unsigned>(kPingTimeoutMs),
             static_cast<unsigned>(preflight.attempts), static_cast<unsigned>(preflight.replies),
             static_cast<unsigned>(success_percent), static_cast<unsigned>(kMinimumSuccessPercent));
    require("test_ready", m5sw::test_support::mark_test_ready());
    run_periodic_traffic(&wifi);
}
