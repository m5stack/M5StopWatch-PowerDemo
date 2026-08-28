#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "m5sw_imu_wakeup.hpp"
#include "m5sw_power.hpp"
#include "m5sw_rtc_wakeup.hpp"

namespace m5sw::wakeup {

enum class WakeSelection : std::uint8_t {
    none,
    rtc,
    imu,
    rtc_imu,
    port,
};

struct WakeSnapshot {
    std::uint8_t pm1_wake = 0;
    std::uint8_t pm1_gpio_irq = 0;
    std::uint8_t pm1_button_irq = 0;
    std::uint8_t pm1_system_irq = 0;
    RtcEvent rtc_events = RtcEvent::none;
    bool imu_any_motion = false;
    bool port = false;

    // Reports the PM1 external-wake classification without exposing driver masks.
    [[nodiscard]] bool pm1_external() const;
    // Reports whether PM1 GPIO0 captured the shared RTC/IMU interrupt edge.
    [[nodiscard]] bool rtc_imu_gpio_irq() const;
};

struct WakeupResult {
    esp_err_t error = ESP_OK;
    int raw_error = 0;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

class WakeController final {
public:
    WakeController() = default;
    WakeController(const WakeController&) = delete;
    WakeController& operator=(const WakeController&) = delete;

    // Initializes PM1 and BMI270 adapters while borrowing the shared I2C bus.
    [[nodiscard]] WakeupResult begin(i2c_bus_handle_t bus);
    // Clears all wake paths, then enables only the selected source combination.
    [[nodiscard]] WakeupResult select_source(
        WakeSelection selection,
        ImuWakeProfile imu_profile = ImuWakeProfile::low_power);
    // Combines PM1, RTC and IMU snapshots without clearing any source flags.
    [[nodiscard]] WakeupResult capture(const RtcFlags& rtc_flags,
                                       const ImuStatus& imu_status,
                                       WakeSnapshot* snapshot);
    // Reads BMI270 interrupt status without clearing it.
    [[nodiscard]] WakeupResult read_imu_status(ImuStatus* status) const;
    // Clears exactly the sources captured in the snapshot and verifies each adapter.
    [[nodiscard]] WakeupResult clear_snapshot(const WakeSnapshot& snapshot);

private:
    [[nodiscard]] WakeupResult from_pm1(m5sw::power::DriverResult result) const;
    [[nodiscard]] WakeupResult from_imu(ImuWakeupResult result) const;

    m5sw::power::Pm1Adapter pm1_;
    ImuWakeup imu_;
    bool initialized_ = false;
};

}  // namespace m5sw::wakeup
