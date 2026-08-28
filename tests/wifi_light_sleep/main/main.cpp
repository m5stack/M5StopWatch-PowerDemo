#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_test_support.hpp"
#include "m5sw_wifi.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "wifi_light_sleep";
constexpr std::uint32_t kConnectTimeoutMs = 15000;
constexpr std::uint32_t kPowerOnSettleMs = 1000;
constexpr int kCpuMaxFrequencyMhz = 240;
constexpr int kCpuMinFrequencyMhz = 40;
constexpr std::uint16_t kExpectedBeaconIntervalTu = 100;
constexpr std::uint8_t kExpectedDtimPeriod = 3;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == kCpuMaxFrequencyMhz,
              "Wi-Fi Light Sleep requires a 240 MHz maximum CPU frequency");
static_assert(CONFIG_XTAL_FREQ == kCpuMinFrequencyMhz,
              "Wi-Fi Light Sleep requires the 40 MHz XTAL as its minimum CPU frequency");
static_assert(CONFIG_M5SW_WIFI_LISTEN_INTERVAL == 3,
              "Wi-Fi Light Sleep requires a listen interval of three beacons");
static_assert(CONFIG_PM_ENABLE == 1, "Dynamic frequency scaling must be enabled");
static_assert(CONFIG_FREERTOS_USE_TICKLESS_IDLE == 1,
              "Automatic Light Sleep requires tickless idle");

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "Wi-Fi Light Sleep setup failed");
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

void configure_power_management()
{
    esp_pm_config_t requested = {};
    requested.max_freq_mhz = kCpuMaxFrequencyMhz;
    requested.min_freq_mhz = kCpuMinFrequencyMhz;
    requested.light_sleep_enable = true;
    require("pm_configure", esp_pm_configure(&requested));

    esp_pm_config_t actual = {};
    require("pm_readback", esp_pm_get_configuration(&actual));
    if (actual.max_freq_mhz != requested.max_freq_mhz ||
        actual.min_freq_mhz != requested.min_freq_mhz ||
        actual.light_sleep_enable != requested.light_sleep_enable) {
        fail("pm_verify", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
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
    require("wifi_connect", wifi.connect(kConnectTimeoutMs));
    if (wifi.state() != m5sw::wifi::State::connected) {
        fail("wifi_state", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }

    require("wifi_ps_max_modem", wifi.set_power_save(m5sw::wifi::PowerSave::max_modem));
    configure_power_management();
    if (wifi.state() != m5sw::wifi::State::connected ||
        wifi.power_save() != m5sw::wifi::PowerSave::max_modem) {
        fail("wifi_power_state", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }

    m5sw::wifi::NetworkInfo info = {};
    require("wifi_info", wifi.network_info(&info));
    if (info.listen_interval != CONFIG_M5SW_WIFI_LISTEN_INTERVAL ||
        info.channel == 0U || info.channel > 14U || info.ipv4_address == 0U) {
        fail("wifi_network", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 BASELINE=l2-active WIFI=connected PS=max_modem LISTEN_INTERVAL=%u CPU_MIN_MHZ=%d CPU_MAX_MHZ=%d LIGHT_SLEEP=1 TICKLESS=1 CHANNEL=%u RSSI_DBM=%d BEACON_TU_EXPECTED=%u DTIM_EXPECTED=%u APP_TRAFFIC=0 SCAN_AFTER_CONNECT=0 L1=on DISPLAY=0 TP=sleep IMU=sleep RTC=retained AUDIO=0 MOTOR=0 EXT5V=0",
             static_cast<unsigned>(info.listen_interval), kCpuMinFrequencyMhz,
             kCpuMaxFrequencyMhz, static_cast<unsigned>(info.channel),
             static_cast<int>(info.rssi_dbm),
             static_cast<unsigned>(kExpectedBeaconIntervalTu),
             static_cast<unsigned>(kExpectedDtimPeriod));
    require("test_ready", m5sw::test_support::mark_test_ready());
    m5sw::test_support::hold_safe_idle();
}
