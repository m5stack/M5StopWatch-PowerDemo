#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"
#include "m5sw_display.hpp"
#include "m5sw_ioe1.hpp"
#include "m5sw_pm1.hpp"
#include "m5sw_test_support.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[] = "display_only";
constexpr std::uint32_t kPowerOnSettleMs = 1000;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240,
              "Display only uses the lowest display setting verified by this project");

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "Display only setup failed");
    m5sw::test_support::hold_safe_idle();
}

void require(const char* stage, esp_err_t error)
{
    if (error != ESP_OK) fail(stage, error, error);
}

void configure_unpowered_inputs()
{
    constexpr std::uint64_t pins =
        (1ULL << m5sw::board::kKey1) |
        (1ULL << m5sw::board::kKey2) |
        (1ULL << m5sw::board::kTouchIrq) |
        (1ULL << m5sw::board::kI2sLrck) |
        (1ULL << m5sw::board::kI2sAdcData) |
        (1ULL << m5sw::board::kI2sBclk) |
        (1ULL << m5sw::board::kI2sMclk) |
        (1ULL << m5sw::board::kI2sDacData);

    gpio_config_t config = {};
    config.pin_bit_mask = pins;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    require("unused_gpio", gpio_config(&config));
}

}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    require("test_support", m5sw::test_support::initialize(&context));

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

    static m5sw::power::Ioe1Adapter ioe1;
    auto ioe1_result = ioe1.begin(bus);
    if (!ioe1_result.ok()) fail("ioe1_begin", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.configure_i2c(false, 100000);
    if (!ioe1_result.ok()) fail("ioe1_i2c", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.stop_all_pwm();
    if (!ioe1_result.ok()) fail("pwm_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::motor_pwm, false);
    if (!ioe1_result.ok()) fail("motor_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::speaker_enable, false);
    if (!ioe1_result.ok()) fail("speaker_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::audio_enable, false);
    if (!ioe1_result.ok()) fail("audio_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::touch_reset, false);
    if (!ioe1_result.ok()) fail("touch_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1.set_pin(m5sw::board::IoePin::mux_select, false);
    if (!ioe1_result.ok()) fail("mux_off", ioe1_result.error, ioe1_result.raw_error);

    configure_unpowered_inputs();

    static m5sw::display::DisplayController display;
    auto display_result = display.begin(bus);
    if (!display_result.ok()) {
        fail("display_begin", display_result.error, display_result.raw_error);
    }
    display_result = display.set_brightness_percent(100);
    if (!display_result.ok()) {
        fail("display_brightness", display_result.error, display_result.raw_error);
    }
    display_result = display.show_white_static();
    if (!display_result.ok()) {
        fail("display_white", display_result.error, display_result.raw_error);
    }

    // L1 powers RTC, IMU, touch and the board I2C pull-ups. Shut it down only
    // after the static display state is complete, and perform no later I2C work.
    pm1_result = pm1.disable_l1_rail_final();
    if (!pm1_result.ok()) fail("l1_off", pm1_result.error, pm1_result.raw_error);

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 DISPLAY=100 WHITE=1 CPU_MHZ=240 L1=0 TP=0 RTC=0 IMU=0 WIFI=0 AUDIO=0 MOTOR=0 EXT5V=0");
    require("test_ready", m5sw::test_support::mark_test_ready());
    m5sw::test_support::hold_safe_idle();
}
