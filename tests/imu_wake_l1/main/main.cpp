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
#define M5SW_IMU_WAKE_L1_REFERENCE_PROFILE 1

namespace {
constexpr char kTag[] = "imu_wake_l1";
constexpr TickType_t kPrepareDelay = pdMS_TO_TICKS(3000);
constexpr char kImuProfileId[] = "imu-wake-l1";
#if M5SW_IMU_WAKE_L1_REFERENCE_PROFILE
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
                                     "IMU wake L1 failed");
    m5sw::test_support::hold_safe_idle();
}

void finish_wake(const m5sw::wakeup::WakeSnapshot& snapshot,
                 const m5sw::wakeup::RtcFlags& rtc_flags,
                 m5sw::wakeup::RtcWakeup* rtc,
                 m5sw::wakeup::WakeController* wake)
{
    const std::uint8_t rtc_events = static_cast<std::uint8_t>(snapshot.rtc_events);
    const bool pm1_external = snapshot.pm1_external();
    const bool rtc_triggered = (rtc_events & kRtcTriggerEvents) != 0U;
    const bool target_evidence = pm1_external ||
                                 snapshot.rtc_imu_gpio_irq() ||
                                 snapshot.imu_any_motion || rtc_triggered;
    const bool valid = pm1_external && snapshot.imu_any_motion && !rtc_triggered;

    if (!target_evidence) return;

    ESP_LOGI(kTag,
             "WAKE_CAPTURE source=imu pm1=0x%02X irq=0x%02X rtc=0x%02X imu=%d",
             snapshot.pm1_wake, snapshot.pm1_gpio_irq,
             static_cast<unsigned>(rtc_events), snapshot.imu_any_motion);

    const auto rtc_clear = rtc->clear_flags(rtc_flags);
    if (!rtc_clear.ok()) fail("rtc_clear", rtc_clear.error, rtc_clear.raw_error);
    const auto wake_clear = wake->clear_snapshot(snapshot);
    if (!wake_clear.ok()) fail("wake_clear", wake_clear.error, wake_clear.raw_error);
    ESP_LOGI(kTag, "WAKE_CLEAR_OK source=imu");

    if (valid) {
        m5sw::test_support::report_final(m5sw::test_support::FinalStatus::pass,
                                         "IMU wake L1");
    } else {
        ESP_LOGE(kTag,
                 "WAKE_FAIL source=imu expected_pm1_external=1 expected_imu=1 "
                 "rtc_must_be=0");
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
    m5sw::wakeup::RtcFlags rtc_flags;
    rtc_result = rtc.read_flags(&rtc_flags);
    if (!rtc_result.ok()) fail("rtc_flags", rtc_result.error, rtc_result.raw_error);

    // BMI270 base initialization performs a soft reset. Capture the retained
    // any-motion evidence first so the L1 wake cause survives driver bring-up.
    m5sw::wakeup::ImuStatus retained_imu;
    auto imu_result = m5sw::wakeup::ImuWakeup::read_retained_status(
        bus, &retained_imu);
    if (!imu_result.ok()) fail("imu_retained", imu_result.error, imu_result.raw_error);

    static m5sw::wakeup::WakeController wake;
    auto wake_result = wake.begin(bus);
    if (!wake_result.ok()) fail("wake_begin", wake_result.error, wake_result.raw_error);
    m5sw::wakeup::WakeSnapshot snapshot;
    wake_result = wake.capture(rtc_flags, retained_imu, &snapshot);
    if (!wake_result.ok()) fail("wake_capture", wake_result.error, wake_result.raw_error);
    finish_wake(snapshot, rtc_flags, &rtc, &wake);

    // No target evidence means a fresh arm cycle. Clear unrelated retained
    // flags before enabling exactly the BMI270 -> PM1 GPIO0 L1 wake chain.
    rtc_result = rtc.clear_flags(rtc_flags);
    if (!rtc_result.ok()) fail("rtc_stale_clear", rtc_result.error, rtc_result.raw_error);
    wake_result = wake.clear_snapshot(snapshot);
    if (!wake_result.ok()) fail("wake_stale_clear", wake_result.error, wake_result.raw_error);
    rtc_result = rtc.disable_interrupts();
    if (!rtc_result.ok()) fail("rtc_disable", rtc_result.error, rtc_result.raw_error);
    wake_result = wake.select_source(m5sw::wakeup::WakeSelection::imu, kImuProfile);
    if (!wake_result.ok()) fail("imu_arm", wake_result.error, wake_result.raw_error);

    static m5sw::power::PowerController power;
    auto power_result = power.begin(bus);
    if (!power_result.ok()) fail("pm1_begin", power_result.error, power_result.raw_error);

    ESP_LOGI(kTag, "BOARD_OK PM1=1 RTC=1 IMU=1 PROFILE=%s profile_id=%s",
             kImuProfileName, kImuProfileId);
    ESP_LOGI(kTag, "WAKE_ARMED source=imu path=pm1_gpio0");
    ESP_ERROR_CHECK(m5sw::test_support::mark_test_ready());
    vTaskDelay(kPrepareDelay);

    power_result = power.enter_l1(m5sw::power::WakeSelection::rtc_imu);
    if (!power_result.ok()) {
        fail("l1_enter", power_result.error, power_result.raw_error);
    }
    m5sw::test_support::hold_safe_idle();
}
