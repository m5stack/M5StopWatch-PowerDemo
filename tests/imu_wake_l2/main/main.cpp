#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_rtc_wakeup.hpp"
#include "m5sw_test_support.hpp"
#include "m5sw_wakeup.hpp"

// Set to 1 for the full reference sensor load, or 0 for the low-power load.
#define M5SW_IMU_WAKE_L2_REFERENCE_PROFILE 1

namespace {
constexpr char kTag[] = "imu_wake_l2";
constexpr TickType_t kPrepareDelay = pdMS_TO_TICKS(3000);
constexpr char kImuProfileId[] = "imu-wake-l2";
#if M5SW_IMU_WAKE_L2_REFERENCE_PROFILE
constexpr auto kImuProfile = m5sw::wakeup::ImuWakeProfile::reference;
constexpr char kImuProfileName[] = "reference";
#else
constexpr auto kImuProfile = m5sw::wakeup::ImuWakeProfile::low_power;
constexpr char kImuProfileName[] = "low_power";
#endif
constexpr std::uint8_t kRtcTriggerEvents =
    static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::alarm) |
    static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::timer);

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage,
             esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                     "IMU wake L2 failed");
    m5sw::test_support::hold_safe_idle();
}

void clear_sources(const m5sw::wakeup::RtcFlags& rtc_flags,
                   const m5sw::wakeup::WakeSnapshot& snapshot,
                   m5sw::wakeup::RtcWakeup* rtc,
                   m5sw::wakeup::WakeController* wake)
{
    const auto rtc_result = rtc->clear_flags(rtc_flags);
    if (!rtc_result.ok()) fail("rtc_clear", rtc_result.error, rtc_result.raw_error);
    const auto wake_result = wake->clear_snapshot(snapshot);
    if (!wake_result.ok()) fail("wake_clear", wake_result.error, wake_result.raw_error);
}

void finish_wake(const m5sw::test_support::TestContext& context,
                 const m5sw::wakeup::WakeSnapshot& snapshot,
                 const m5sw::wakeup::RtcFlags& rtc_flags,
                 m5sw::wakeup::RtcWakeup* rtc,
                 m5sw::wakeup::WakeController* wake)
{
    if (context.wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED) return;

    const std::uint8_t rtc_events = static_cast<std::uint8_t>(snapshot.rtc_events);
    const bool pm1_gpio0_irq = snapshot.rtc_imu_gpio_irq();
    const bool rtc_triggered = (rtc_events & kRtcTriggerEvents) != 0U;
    const bool valid = context.wake_cause == ESP_SLEEP_WAKEUP_EXT0 &&
                       pm1_gpio0_irq && snapshot.imu_any_motion && !rtc_triggered;

    ESP_LOGI(kTag,
             "WAKE_CAPTURE source=imu esp=%s pm1=0x%02X irq=0x%02X "
             "btn=0x%02X sys=0x%02X rtc=0x%02X imu=%d",
             m5sw::test_support::wake_cause_name(context.wake_cause),
             snapshot.pm1_wake, snapshot.pm1_gpio_irq, snapshot.pm1_button_irq,
             snapshot.pm1_system_irq,
             static_cast<unsigned>(rtc_events), snapshot.imu_any_motion);
    clear_sources(rtc_flags, snapshot, rtc, wake);
    ESP_LOGI(kTag, "WAKE_CLEAR_OK source=imu");

    if (valid) {
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::pass,
                                         "IMU wake L2");
    } else {
        ESP_LOGE(kTag,
                 "WAKE_FAIL source=imu expected_esp=ext0 "
                 "expected_pm1_gpio0_irq=1 expected_imu=1 rtc_must_be=0");
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail,
                                         "wake source mismatch");
    }
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

    static m5sw::wakeup::RtcWakeup rtc;
    auto rtc_result = rtc.begin(bus);
    if (!rtc_result.ok()) fail("rtc_begin", rtc_result.error, rtc_result.raw_error);
    m5sw::wakeup::RtcFlags retained_rtc;
    rtc_result = rtc.read_flags(&retained_rtc);
    if (!rtc_result.ok()) fail("rtc_flags", rtc_result.error, rtc_result.raw_error);

    // Preserve the Deep Sleep wake evidence before BMI270 base initialization
    // soft-resets the sensor and clears its latched interrupt status.
    m5sw::wakeup::ImuStatus retained_imu;
    auto imu_result = m5sw::wakeup::ImuWakeup::read_retained_status(
        bus, &retained_imu);
    if (!imu_result.ok()) fail("imu_retained", imu_result.error, imu_result.raw_error);

    static m5sw::wakeup::WakeController wake;
    auto wake_result = wake.begin(bus);
    if (!wake_result.ok()) fail("wake_begin", wake_result.error, wake_result.raw_error);
    m5sw::wakeup::WakeSnapshot retained_snapshot;
    wake_result = wake.capture(retained_rtc, retained_imu, &retained_snapshot);
    if (!wake_result.ok()) fail("wake_capture", wake_result.error, wake_result.raw_error);
    finish_wake(context, retained_snapshot, retained_rtc, &rtc, &wake);

    // A normal cold boot starts one arm cycle. Stale retained flags are cleared
    // before the validated L2 shutdown sequence is applied.
    clear_sources(retained_rtc, retained_snapshot, &rtc, &wake);
    rtc_result = rtc.disable_interrupts();
    if (!rtc_result.ok()) fail("rtc_disable", rtc_result.error, rtc_result.raw_error);

    static m5sw::power::PowerController power;
    auto power_result = power.begin(bus);
    if (!power_result.ok()) fail("pm1_begin", power_result.error, power_result.raw_error);
    power_result = power.prepare_l2(m5sw::power::WakeSelection::rtc_imu);
    if (!power_result.ok()) fail("l2_prepare", power_result.error, power_result.raw_error);

    // Arm motion after board preparation but before enter_l2() performs the
    // final IOE1/PM1 idle-sleep handoff.
    wake_result = wake.select_source(m5sw::wakeup::WakeSelection::imu, kImuProfile);
    if (!wake_result.ok()) fail("imu_arm", wake_result.error, wake_result.raw_error);

    ESP_LOGI(kTag,
             "BOARD_OK PM1=1 IOE1=1 RTC=1 IMU=1 PROFILE=%s profile_id=%s",
             kImuProfileName, kImuProfileId);
    ESP_LOGI(kTag, "WAKE_ARMED source=imu path=pm1_gpio0+esp_ext0");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());
    vTaskDelay(kPrepareDelay);

    // Match the validated reference path: after arming BMI270, perform no
    // additional RTC/PM1/BMI270 transaction before the final L2 handoff.
    power_result = power.enter_l2(m5sw::power::WakeSelection::rtc_imu);
    fail("l2_returned", power_result.error, power_result.raw_error);
}
