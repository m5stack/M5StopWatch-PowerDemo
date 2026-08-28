#include <cstdint>

#include "bmi270_tools.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"
#include "m5sw_ioe1.hpp"
#include "m5sw_pm1.hpp"
#include "m5sw_test_support.hpp"
#include "sdkconfig.h"

namespace {

constexpr char kTag[]              = "imu_sampling";
constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(10);
constexpr char kProfileName[]      = "reference";
constexpr auto kAccelRange         = bmi270_tools::ACCEL_RANGE_4G;
constexpr auto kGyroRange          = bmi270_tools::GYRO_RANGE_1000;
constexpr auto kAccelOdr           = bmi270_tools::ODR_200HZ;
constexpr auto kGyroOdr            = bmi270_tools::ODR_200HZ;

static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240, "IMU sampling requires the fixed project CPU configuration");
static_assert(kSamplePeriod > 0, "The RTOS tick must represent a 10 ms period");

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage, esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "IMU sampling failed");
    m5sw::test_support::hold_safe_idle();
}

void require(const char* stage, esp_err_t error)
{
    if (error != ESP_OK) fail(stage, error, error);
}

void configure_unused_inputs()
{
    constexpr std::uint64_t pins = (1ULL << m5sw::board::kKey1) | (1ULL << m5sw::board::kKey2) |
                                   (1ULL << m5sw::board::kTouchIrq) | (1ULL << m5sw::board::kI2sLrck) |
                                   (1ULL << m5sw::board::kI2sAdcData) | (1ULL << m5sw::board::kI2sBclk) |
                                   (1ULL << m5sw::board::kI2sMclk) | (1ULL << m5sw::board::kI2sDacData);

    gpio_config_t config = {};
    config.pin_bit_mask  = pins;
    config.mode          = GPIO_MODE_INPUT;
    config.pull_up_en    = GPIO_PULLUP_DISABLE;
    config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    config.intr_type     = GPIO_INTR_DISABLE;
    require("unused_gpio", gpio_config(&config));
}

void disable_non_imu_loads(i2c_bus_handle_t bus, m5sw::power::Pm1Adapter* pm1, m5sw::power::Ioe1Adapter* ioe1)
{
    auto pm1_result = pm1->begin(bus);
    if (!pm1_result.ok()) fail("pm1_begin", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1->set_external_5v_output(false);
    if (!pm1_result.ok()) fail("external_5v_off", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1->set_status_led(false);
    if (!pm1_result.ok()) fail("status_led_off", pm1_result.error, pm1_result.raw_error);
    pm1_result = pm1->set_l1_rail(true);
    if (!pm1_result.ok()) fail("l1_on", pm1_result.error, pm1_result.raw_error);

    auto ioe1_result = ioe1->begin(bus);
    if (!ioe1_result.ok()) fail("ioe1_begin", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1->configure_i2c(false, 100000);
    if (!ioe1_result.ok()) fail("ioe1_i2c", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1->stop_all_pwm();
    if (!ioe1_result.ok()) fail("pwm_off", ioe1_result.error, ioe1_result.raw_error);
    ioe1_result = ioe1->set_pin(m5sw::board::IoePin::motor_pwm, false);
    if (!ioe1_result.ok()) fail("motor_off", ioe1_result.error, ioe1_result.raw_error);

    constexpr m5sw::board::IoePin off_pins[] = {
        m5sw::board::IoePin::speaker_enable, m5sw::board::IoePin::audio_enable,  m5sw::board::IoePin::touch_reset,
        m5sw::board::IoePin::mux_select,     m5sw::board::IoePin::display_reset, m5sw::board::IoePin::l3b_enable,
    };
    for (const auto pin : off_pins) {
        ioe1_result = ioe1->set_pin(pin, false);
        if (!ioe1_result.ok()) fail("ioe1_load_off", ioe1_result.error, ioe1_result.raw_error);
    }
    configure_unused_inputs();
}

void configure_and_verify_imu(bmi270_tools* imu)
{
    require("imu_enable", imu->enable_default_sensors());

    std::uint8_t registers[4] = {};
    require("imu_config_read", i2c_bus_read_bytes(imu->device(), BMI2_ACC_CONF_ADDR, sizeof(registers), registers));
    const bool verified = (registers[0] & BMI2_ACC_ODR_MASK) == static_cast<std::uint8_t>(kAccelOdr) &&
                          (registers[1] & BMI2_ACC_RANGE_MASK) == static_cast<std::uint8_t>(kAccelRange) &&
                          (registers[2] & BMI2_GYR_ODR_MASK) == static_cast<std::uint8_t>(kGyroOdr) &&
                          (registers[3] & BMI2_GYR_RANGE_MASK) == static_cast<std::uint8_t>(kGyroRange);
    if (!verified) {
        fail("imu_config_verify", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
}

}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    require("test_support", m5sw::test_support::initialize(&context));

    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);
    require("i2c", m5sw::board::initialize_i2c(m5sw::board::I2cSpeed::standard));
    i2c_bus_handle_t bus = nullptr;
    require("i2c_claim", m5sw::board::acquire_i2c(&bus));

    static m5sw::power::Pm1Adapter pm1;
    static m5sw::power::Ioe1Adapter ioe1;
    disable_non_imu_loads(bus, &pm1, &ioe1);

    constexpr m5sw::board::DeviceMask required_devices = m5sw::board::device_mask(m5sw::board::Device::pm1) |
                                                         m5sw::board::device_mask(m5sw::board::Device::ioe1) |
                                                         m5sw::board::device_mask(m5sw::board::Device::imu);
    m5sw::board::ProbeReport probe = {};
    require("board_probe", m5sw::board::probe_devices(&probe, required_devices));

    static bmi270_tools imu(m5sw::board::kImuAddress, 100000);
    require("imu_begin", imu.init(bus, nullptr, true, bmi270_tools::MODE_CONTEXT));
    configure_and_verify_imu(&imu);

    vTaskDelay(pdMS_TO_TICKS(20));
    bmi270_tools::sensor_data_t initial_sample = {};
    require("imu_initial_read", imu.get_sensor_data(initial_sample));

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 IMU=1 PROFILE=%s profile_id=imu-sampling "
             "ACCEL_HZ=%u ACCEL_G=%u GYRO_HZ=%u GYRO_DPS=%u AUX=%d "
             "DISPLAY=0 TP=0 WIFI=0 AUDIO=0 MOTOR=0 EXT5V=0",
             kProfileName, 200U, 4U, 200U, 1000U, 1);
    require("test_ready", m5sw::test_support::mark_test_ready());

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, kSamplePeriod);
        bmi270_tools::sensor_data_t sample = {};
        const esp_err_t result             = imu.get_sensor_data(sample);
        if (result != ESP_OK) fail("imu_runtime_read", result, result);
    }
}
