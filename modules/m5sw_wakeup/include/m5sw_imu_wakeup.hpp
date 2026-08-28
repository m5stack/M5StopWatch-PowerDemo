#pragma once

#include <cstdint>

#include "bmi270_tools.h"
#include "esp_err.h"
#include "i2c_bus.h"

namespace m5sw::wakeup {

enum class ImuWakeProfile : std::uint8_t {
    low_power,
    reference,
};

struct ImuStatus {
    std::uint16_t raw = 0;
    bool any_motion   = false;
};

struct ImuWakeupResult {
    esp_err_t error = ESP_OK;
    int raw_error   = 0;

    [[nodiscard]] bool ok() const
    {
        return error == ESP_OK;
    }
};

class ImuWakeup final {
public:
    ImuWakeup()  = default;
    ~ImuWakeup() = default;

    ImuWakeup(const ImuWakeup&)            = delete;
    ImuWakeup& operator=(const ImuWakeup&) = delete;

    // Reads retained interrupt status before begin() soft-resets the BMI270.
    [[nodiscard]] static ImuWakeupResult read_retained_status(i2c_bus_handle_t bus, ImuStatus* status);
    // Initializes the BMI270 base API and borrows the supplied I2C bus.
    [[nodiscard]] ImuWakeupResult begin(i2c_bus_handle_t bus);
    // Configures any-motion and the selected sensor load on INT1.
    [[nodiscard]] ImuWakeupResult configure_any_motion(ImuWakeProfile profile = ImuWakeProfile::low_power,
                                                       std::uint8_t duration = 10, std::uint8_t threshold = 30);
    // Reads the current BMI270 feature-interrupt status for wake classification.
    [[nodiscard]] ImuWakeupResult read_status(ImuStatus* status) const;
    // Consumes pending BMI270 interrupt status after the caller records evidence.
    [[nodiscard]] ImuWakeupResult clear_status();
    // Disables configured sensors and INT1, leaving the adapter disarmed.
    [[nodiscard]] ImuWakeupResult disable();

private:
    bmi270_tools imu_;
    bool initialized_           = false;
    bool any_motion_configured_ = false;
    ImuWakeProfile profile_     = ImuWakeProfile::low_power;
};

}  // namespace m5sw::wakeup
