#include <cstdint>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"
#include "m5sw_display.hpp"
#include "m5sw_imu_wakeup.hpp"
#include "m5sw_ioe1.hpp"
#include "m5sw_pm1.hpp"
#include "m5sw_rtc_wakeup.hpp"
#include "m5sw_test_support.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "normal_active";
constexpr std::uint8_t kTouchChipIdRegister = 0xA7;
constexpr std::uint8_t kTouchFirmwareRegister = 0xA9;
constexpr std::uint32_t kTouchResetLowMs = 10;
constexpr std::uint32_t kTouchResetReadyMs = 50;
constexpr std::uint32_t kPowerOnSettleMs = 1000;
constexpr std::uint32_t kDisplayPeriodMs = 1000U / m5sw::display::kNormalRefreshHz;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240,
              "Normal active requires a fixed 240 MHz CPU configuration");
static_assert(m5sw::display::kNormalRefreshHz == 1,
              "Normal active requires an exact 1 Hz display load");

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "Normal active setup failed");
    m5sw::test_support::hold_safe_idle();
}

void require(const char* stage, esp_err_t error)
{
    if (error != ESP_OK) fail(stage, error, error);
}

[[noreturn]] void runtime_fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "RUNTIME_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "Normal active runtime failed");
    m5sw::test_support::hold_safe_idle();
}

void configure_buttons()
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << m5sw::board::kKey1) |
                          (1ULL << m5sw::board::kKey2);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    require("buttons_config", gpio_config(&config));
}

i2c_bus_device_handle_t initialize_touch(i2c_bus_handle_t bus,
                                         m5sw::power::Ioe1Adapter* ioe1)
{
    auto result = ioe1->set_pin(m5sw::board::IoePin::touch_reset, false);
    if (!result.ok()) fail("touch_reset_low", result.error, result.raw_error);
    vTaskDelay(pdMS_TO_TICKS(kTouchResetLowMs));
    result = ioe1->set_pin(m5sw::board::IoePin::touch_reset, true);
    if (!result.ok()) fail("touch_reset_high", result.error, result.raw_error);
    vTaskDelay(pdMS_TO_TICKS(kTouchResetReadyMs));

    gpio_config_t irq_config = {};
    irq_config.pin_bit_mask = 1ULL << m5sw::board::kTouchIrq;
    irq_config.mode = GPIO_MODE_INPUT;
    irq_config.pull_up_en = GPIO_PULLUP_ENABLE;
    irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq_config.intr_type = GPIO_INTR_DISABLE;
    require("touch_irq", gpio_config(&irq_config));

    i2c_bus_device_handle_t device = i2c_bus_device_create(
        bus, m5sw::board::kTouchAddress, 100000);
    if (device == nullptr) fail("touch_device", ESP_ERR_NO_MEM, ESP_ERR_NO_MEM);

    std::uint8_t chip_id = 0;
    std::uint8_t firmware = 0;
    esp_err_t error = i2c_bus_read_byte(device, kTouchChipIdRegister, &chip_id);
    if (error != ESP_OK) fail("touch_chip_id", error, error);
    error = i2c_bus_read_byte(device, kTouchFirmwareRegister, &firmware);
    if (error != ESP_OK) fail("touch_firmware", error, error);
    if (chip_id == 0U || firmware == 0U) {
        fail("touch_identity", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
    return device;
}

}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    require("test_support", m5sw::test_support::initialize(&context));
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0U) {
        fail("psram", ESP_ERR_NOT_FOUND, ESP_ERR_NOT_FOUND);
    }

    gpio_deep_sleep_hold_dis();
    vTaskDelay(pdMS_TO_TICKS(kPowerOnSettleMs));
    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);
    require("i2c", m5sw::board::initialize_i2c(m5sw::board::I2cSpeed::standard));
    i2c_bus_handle_t bus = nullptr;
    require("i2c_claim", m5sw::board::acquire_i2c(&bus));

    static m5sw::power::Pm1Adapter pm1;
    auto pm1_result = pm1.begin(bus);
    if (!pm1_result.ok()) fail("pm1_begin", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_external_5v_output(false);
    if (!pm1_result.ok()) fail("external_5v_off", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_status_led(false);
    if (!pm1_result.ok()) fail("status_led_off", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1.set_power_config(
        M5PM1_PWR_CFG_CHG_EN | M5PM1_PWR_CFG_LDO_EN,
        M5PM1_PWR_CFG_CHG_EN | M5PM1_PWR_CFG_LDO_EN);
    if (!pm1_result.ok()) {
        fail("active_power", pm1_result.error, pm1_result.raw_error);
    }
    pm1_result = pm1.set_ldo_hold(true);
    if (!pm1_result.ok()) fail("l1_hold", pm1_result.error, pm1_result.raw_error);

    static m5sw::power::Ioe1Adapter ioe1;
    auto ioe1_result = ioe1.begin(bus);
    if (!ioe1_result.ok()) fail("ioe1_begin", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.configure_i2c(false, 100000);
    if (!ioe1_result.ok()) fail("ioe1_i2c", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_motor_percent(0);
    if (!ioe1_result.ok()) fail("motor_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::speaker_enable, false);
    if (!ioe1_result.ok()) fail("speaker_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::audio_enable, false);
    if (!ioe1_result.ok()) fail("audio_off", ioe1_result.error, ioe1_result.raw_error);

    configure_buttons();
    static i2c_bus_device_handle_t touch_device = initialize_touch(bus, &ioe1);
    (void)touch_device;

    static m5sw::wakeup::RtcWakeup rtc;
    auto rtc_result = rtc.begin(bus);
    if (!rtc_result.ok()) fail("rtc_begin", rtc_result.error, rtc_result.raw_error);
    rtc_result = rtc.disable_interrupts();
    if (!rtc_result.ok()) fail("rtc_irq_off", rtc_result.error, rtc_result.raw_error);

    static m5sw::wakeup::ImuWakeup imu;
    const auto imu_result = imu.begin(bus);
    if (!imu_result.ok()) fail("imu_begin", imu_result.error, imu_result.raw_error);

    static m5sw::display::DisplayController display;
    auto display_result = display.begin(bus);
    if (!display_result.ok()) {
        fail("display_begin", display_result.error, display_result.raw_error);
    }
    display_result = display.set_brightness_percent(50);
    if (!display_result.ok()) {
        fail("display_brightness", display_result.error, display_result.raw_error);
    }
    display_result = display.draw_normal_frame(0);
    if (!display_result.ok()) {
        fail("display_initial_frame", display_result.error, display_result.raw_error);
    }

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 L1=on DISPLAY=50 TP=1 BUTTON=2 RTC=1 IMU=1 WIFI=0 AUDIO=0 MOTOR=0 EXT5V=0");
    require("test_ready", m5sw::test_support::mark_test_ready());

    TickType_t last_wake = xTaskGetTickCount();
    std::uint32_t elapsed_seconds = 0;
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kDisplayPeriodMs));
        ++elapsed_seconds;
        display_result = display.draw_normal_frame(elapsed_seconds);
        if (!display_result.ok()) {
            runtime_fail("display_frame", display_result.error,
                         display_result.raw_error);
        }
    }
}
