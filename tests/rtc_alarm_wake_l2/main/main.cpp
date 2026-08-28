#include <cstdint>
#include <ctime>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5sw_board.hpp"
#include "m5sw_power.hpp"
#include "m5sw_rtc_wakeup.hpp"
#include "m5sw_test_support.hpp"
#include "m5sw_wakeup.hpp"

namespace {
constexpr char kTag[] = "rtc_alarm_wake_l2";

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage, esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "RTC alarm wake L2 failed");
    m5sw::test_support::hold_safe_idle();
}

void clear_sources(const m5sw::wakeup::RtcFlags& rtc_flags, const m5sw::wakeup::WakeSnapshot& snapshot,
                   m5sw::wakeup::RtcWakeup* rtc, m5sw::wakeup::WakeController* wake)
{
    const auto rtc_result = rtc->clear_flags(rtc_flags);
    if (!rtc_result.ok()) fail("rtc_clear", rtc_result.error, rtc_result.raw_error);
    const auto wake_result = wake->clear_snapshot(snapshot);
    if (!wake_result.ok()) fail("wake_clear", wake_result.error, wake_result.raw_error);
}

void finish_wake(const m5sw::test_support::TestContext& context, const m5sw::wakeup::WakeSnapshot& snapshot,
                 const m5sw::wakeup::RtcFlags& rtc_flags, m5sw::wakeup::RtcWakeup* rtc,
                 m5sw::wakeup::WakeController* wake)
{
    if (context.wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED) return;

    const std::uint8_t rtc_events = static_cast<std::uint8_t>(snapshot.rtc_events);
    const bool pm1_gpio0_irq      = snapshot.rtc_imu_gpio_irq();
    const bool rtc_alarm          = (rtc_events & static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::alarm)) != 0U;
    const bool rtc_timer          = (rtc_events & static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::timer)) != 0U;
    const bool valid = context.wake_cause == ESP_SLEEP_WAKEUP_EXT0 && pm1_gpio0_irq && rtc_alarm && !rtc_timer &&
                       !snapshot.imu_any_motion && !snapshot.port;

    ESP_LOGI(kTag,
             "WAKE_CAPTURE source=rtc_alarm esp=%s pm1=0x%02X irq=0x%02X "
             "btn=0x%02X sys=0x%02X rtc=0x%02X imu=%d port=%d",
             m5sw::test_support::wake_cause_name(context.wake_cause), snapshot.pm1_wake, snapshot.pm1_gpio_irq,
             snapshot.pm1_button_irq, snapshot.pm1_system_irq, static_cast<unsigned>(rtc_events),
             snapshot.imu_any_motion, snapshot.port);
    clear_sources(rtc_flags, snapshot, rtc, wake);
    ESP_LOGI(kTag, "WAKE_CLEAR_OK source=rtc_alarm");

    if (valid) {
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::pass, "RTC alarm wake L2");
    } else {
        ESP_LOGE(kTag,
                 "WAKE_FAIL source=rtc_alarm expected_esp=ext0 "
                 "expected_pm1_gpio0_irq=1 expected_alarm=1 "
                 "timer_imu_port_must_be=0");
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "wake source mismatch");
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

    i2c_bus_handle_t bus         = nullptr;
    const esp_err_t claim_result = m5sw::board::acquire_i2c(&bus);
    if (claim_result != ESP_OK) fail("shared_bus", claim_result, claim_result);

    static m5sw::wakeup::RtcWakeup rtc;
    auto rtc_result = rtc.begin(bus);
    if (!rtc_result.ok()) fail("rtc_begin", rtc_result.error, rtc_result.raw_error);
    m5sw::wakeup::RtcFlags retained_rtc;
    rtc_result = rtc.read_flags(&retained_rtc);
    if (!rtc_result.ok()) fail("rtc_flags", rtc_result.error, rtc_result.raw_error);

    // Preserve the non-target BMI270 evidence before WakeController performs
    // BMI270 base initialization and clears its latched interrupt status.
    m5sw::wakeup::ImuStatus retained_imu;
    const auto imu_result = m5sw::wakeup::ImuWakeup::read_retained_status(bus, &retained_imu);
    if (!imu_result.ok()) fail("imu_retained", imu_result.error, imu_result.raw_error);

    static m5sw::wakeup::WakeController wake;
    auto wake_result = wake.begin(bus);
    if (!wake_result.ok()) fail("wake_begin", wake_result.error, wake_result.raw_error);
    m5sw::wakeup::WakeSnapshot retained_snapshot;
    wake_result = wake.capture(retained_rtc, retained_imu, &retained_snapshot);
    if (!wake_result.ok()) fail("wake_capture", wake_result.error, wake_result.raw_error);
    finish_wake(context, retained_snapshot, retained_rtc, &rtc, &wake);

    clear_sources(retained_rtc, retained_snapshot, &rtc, &wake);
    rtc_result = rtc.disable_interrupts();
    if (!rtc_result.ok()) fail("rtc_disable", rtc_result.error, rtc_result.raw_error);

    static m5sw::power::PowerController power;
    auto power_result = power.begin(bus);
    if (!power_result.ok()) fail("pm1_begin", power_result.error, power_result.raw_error);
    power_result = power.prepare_l2(m5sw::power::WakeSelection::rtc_imu);
    if (!power_result.ok()) fail("l2_prepare", power_result.error, power_result.raw_error);

    // Arm RTC only after the validated IOE1/PM1 L2 convergence. This keeps
    // preparation transactions from consuming or replacing the target state.
    wake_result = wake.select_source(m5sw::wakeup::WakeSelection::rtc);
    if (!wake_result.ok()) fail("rtc_arm", wake_result.error, wake_result.raw_error);

    std::tm baseline = {};
    baseline.tm_year = 2026 - 1900;
    baseline.tm_mon  = 7;
    baseline.tm_mday = 26;
    baseline.tm_hour = 12;
    baseline.tm_min  = 0;
    baseline.tm_sec  = 0;
    baseline.tm_wday = 3;
    std::tm target   = {};
    rtc_result       = rtc.configure_alarm_120s(baseline, &target);
    if (!rtc_result.ok()) {
        fail("rtc_alarm_config", rtc_result.error, rtc_result.raw_error);
    }

    m5sw::wakeup::RtcFlags setup_flags;
    rtc_result = rtc.read_flags(&setup_flags);
    if (!rtc_result.ok()) fail("rtc_setup_flags", rtc_result.error, rtc_result.raw_error);
    rtc_result = rtc.clear_flags(setup_flags);
    if (!rtc_result.ok()) {
        fail("rtc_setup_clear", rtc_result.error, rtc_result.raw_error);
    }

    ESP_LOGI(kTag,
             "RTC_CONFIG_OK baseline=%04d-%02d-%02dT%02d:%02d:%02d "
             "target=%04d-%02d-%02dT%02d:%02d:%02d",
             baseline.tm_year + 1900, baseline.tm_mon + 1, baseline.tm_mday, baseline.tm_hour, baseline.tm_min,
             baseline.tm_sec, target.tm_year + 1900, target.tm_mon + 1, target.tm_mday, target.tm_hour, target.tm_min,
             target.tm_sec);
    ESP_LOGI(kTag, "BOARD_OK PM1=1 IOE1=1 RTC=1 IMU=1");
    ESP_LOGI(kTag, "WAKE_ARMED source=rtc_alarm path=pm1_gpio0+esp_ext0 delay=120s");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());

    power_result = power.enter_l2(m5sw::power::WakeSelection::rtc_imu);
    fail("l2_returned", power_result.error, power_result.raw_error);
}
