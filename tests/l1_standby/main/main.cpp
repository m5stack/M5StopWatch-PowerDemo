#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_imu_wakeup.hpp"
#include "m5sw_power.hpp"
#include "m5sw_rtc_wakeup.hpp"
#include "m5sw_test_support.hpp"

namespace {
constexpr char kTag[] = "l1_standby";
constexpr TickType_t kPrepareDelay = pdMS_TO_TICKS(3000);

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "L1 preparation failed");
    m5sw::test_support::hold_safe_idle();
}
}  // namespace

extern "C" void app_main(void)
{
    m5sw::test_support::TestContext context = {};
    ESP_ERROR_CHECK(m5sw::test_support::initialize(&context));
    esp_log_level_set("i2c_bus", ESP_LOG_ERROR);

    const esp_err_t i2c_result = m5sw::board::initialize_i2c();
    if (i2c_result != ESP_OK) fail("i2c", i2c_result, i2c_result);

    i2c_bus_handle_t bus = nullptr;
    const esp_err_t claim_result = m5sw::board::acquire_i2c(&bus);
    if (claim_result != ESP_OK) fail("shared_bus", claim_result, claim_result);

    // Keep only the L1 RTC/IMU path initialized. No RTC alarm, timer, IMU
    // motion interrupt, IOE1 load, or Port wake source is enabled.
    static m5sw::wakeup::RtcWakeup rtc;
    const auto rtc_result = rtc.begin(bus);
    if (!rtc_result.ok()) fail("rtc_begin", rtc_result.error, rtc_result.raw_error);
    const auto rtc_irq_result = rtc.disable_interrupts();
    if (!rtc_irq_result.ok()) {
        fail("rtc_irq_disable", rtc_irq_result.error, rtc_irq_result.raw_error);
    }

    static m5sw::wakeup::ImuWakeup imu;
    const auto imu_result = imu.begin(bus);
    if (!imu_result.ok()) fail("imu_begin", imu_result.error, imu_result.raw_error);
    const auto imu_disable_result = imu.disable();
    if (!imu_disable_result.ok()) {
        fail("imu_disable", imu_disable_result.error, imu_disable_result.raw_error);
    }

    static m5sw::power::PowerController power;
    const auto power_result = power.begin(bus);
    if (!power_result.ok()) fail("pm1_begin", power_result.error, power_result.raw_error);

    ESP_LOGI(kTag, "BOARD_OK PM1=1 RTC=1 IMU=1");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());
    vTaskDelay(kPrepareDelay);

    const auto l1_result = power.enter_l1();
    if (!l1_result.ok()) {
        fail("l1_enter", l1_result.error, l1_result.raw_error);
    }

    // PM1 L1 shutdown is expected to remove the console path while retaining
    // the configured L1 rails. No access or logging is allowed after this call.
    m5sw::test_support::hold_safe_idle();
}
