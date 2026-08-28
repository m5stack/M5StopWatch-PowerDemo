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
constexpr char kTag[] = "rtc_alarm_wake_l1";
constexpr std::uint8_t kRtcTriggerEvents =
    static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::alarm) | static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::timer);

[[noreturn]] void fail(const char* stage, esp_err_t error, int raw_error)
{
    ESP_LOGE(kTag, "BOARD_FAIL stage=%s error=%s raw=%d", stage, esp_err_to_name(error), raw_error);
    m5sw::test_support::report_final(m5sw::test_support::FinalStatus::fail, "RTC alarm wake L1 failed");
    m5sw::test_support::hold_safe_idle();
}

void verify_two_minute_boundary()
{
    std::tm boundary = {};
    boundary.tm_year = 2026 - 1900;
    boundary.tm_mon  = 7;
    boundary.tm_mday = 26;
    boundary.tm_hour = 12;
    boundary.tm_min  = 59;
    boundary.tm_sec  = 0;
    boundary.tm_wday = 3;

    std::tm first_minute  = {};
    std::tm target        = {};
    const bool normalized = m5sw::wakeup::RtcWakeup::normalize_next_minute(boundary, &first_minute) &&
                            m5sw::wakeup::RtcWakeup::normalize_next_minute(first_minute, &target);
    if (!normalized || target.tm_year != boundary.tm_year || target.tm_mon != boundary.tm_mon ||
        target.tm_mday != boundary.tm_mday || target.tm_hour != 13 || target.tm_min != 1 || target.tm_sec != 0) {
        fail("rtc_boundary", ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE);
    }
    ESP_LOGI(kTag, "RTC_BOUNDARY_OK delay=120s minute=59 target=13:01");
}

void clear_sources(const m5sw::wakeup::RtcFlags& rtc_flags, const m5sw::wakeup::WakeSnapshot& snapshot,
                   m5sw::wakeup::RtcWakeup* rtc, m5sw::wakeup::WakeController* wake)
{
    const auto rtc_result = rtc->clear_flags(rtc_flags);
    if (!rtc_result.ok()) fail("rtc_clear", rtc_result.error, rtc_result.raw_error);
    const auto wake_result = wake->clear_snapshot(snapshot);
    if (!wake_result.ok()) fail("wake_clear", wake_result.error, wake_result.raw_error);
}

void finish_wake(const m5sw::wakeup::WakeSnapshot& snapshot, const m5sw::wakeup::RtcFlags& rtc_flags,
                 m5sw::wakeup::RtcWakeup* rtc, m5sw::wakeup::WakeController* wake)
{
    const std::uint8_t rtc_events = static_cast<std::uint8_t>(snapshot.rtc_events);
    const bool pm1_external       = snapshot.pm1_external();
    const bool rtc_alarm          = (rtc_events & static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::alarm)) != 0U;
    const bool rtc_timer          = (rtc_events & static_cast<std::uint8_t>(m5sw::wakeup::RtcEvent::timer)) != 0U;
    const bool target_evidence    = pm1_external || snapshot.rtc_imu_gpio_irq() || snapshot.imu_any_motion ||
                                 (rtc_events & kRtcTriggerEvents) != 0U;
    if (!target_evidence) return;

    const bool valid = pm1_external && rtc_alarm && !rtc_timer && !snapshot.imu_any_motion;
    ESP_LOGI(kTag, "WAKE_CAPTURE source=rtc_alarm pm1=0x%02X irq=0x%02X rtc=0x%02X imu=%d", snapshot.pm1_wake,
             snapshot.pm1_gpio_irq, static_cast<unsigned>(rtc_events), snapshot.imu_any_motion);
    clear_sources(rtc_flags, snapshot, rtc, wake);
    ESP_LOGI(kTag, "WAKE_CLEAR_OK source=rtc_alarm");

    if (valid) {
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::pass, "RTC alarm wake L1");
    } else {
        ESP_LOGE(kTag,
                 "WAKE_FAIL source=rtc_alarm expected_pm1_external=1 "
                 "expected_alarm=1 timer_must_be=0 imu_must_be=0");
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

    // WakeController initialization soft-resets BMI270. Preserve its retained
    // status first so an unexpected IMU source cannot be accepted as RTC.
    m5sw::wakeup::ImuStatus retained_imu;
    const auto imu_result = m5sw::wakeup::ImuWakeup::read_retained_status(bus, &retained_imu);
    if (!imu_result.ok()) fail("imu_retained", imu_result.error, imu_result.raw_error);

    static m5sw::wakeup::WakeController wake;
    auto wake_result = wake.begin(bus);
    if (!wake_result.ok()) fail("wake_begin", wake_result.error, wake_result.raw_error);
    m5sw::wakeup::WakeSnapshot retained_snapshot;
    wake_result = wake.capture(retained_rtc, retained_imu, &retained_snapshot);
    if (!wake_result.ok()) fail("wake_capture", wake_result.error, wake_result.raw_error);
    finish_wake(retained_snapshot, retained_rtc, &rtc, &wake);

    clear_sources(retained_rtc, retained_snapshot, &rtc, &wake);
    verify_two_minute_boundary();

    static m5sw::power::PowerController power;
    auto power_result = power.begin(bus);
    if (!power_result.ok()) fail("pm1_begin", power_result.error, power_result.raw_error);
    power_result = power.prepare_l1(m5sw::power::WakeSelection::rtc_imu);
    if (!power_result.ok()) fail("l1_prepare", power_result.error, power_result.raw_error);

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

    // Setting the known clock can raise a non-target update flag. Clear all
    // setup-window flags before the one-shot wake test begins.
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
    ESP_LOGI(kTag, "BOARD_OK PM1=1 RTC=1 IMU=1");
    ESP_LOGI(kTag, "WAKE_ARMED source=rtc_alarm path=pm1_gpio0 delay=120s");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());

    power_result = power.enter_l1(m5sw::power::WakeSelection::rtc_imu);
    if (!power_result.ok()) fail("l1_enter", power_result.error, power_result.raw_error);
    m5sw::test_support::hold_safe_idle();
}
