#pragma once

#include <cstdint>

#include "M5PM1.h"
#include "esp_err.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"

namespace m5sw::power {

struct DriverResult {
    esp_err_t error = ESP_OK;
    int raw_error = M5PM1_OK;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

enum class Pm1Gpio : std::uint8_t {
    rtc_imu = M5PM1_GPIO_NUM_0,
    port = M5PM1_GPIO_NUM_4,
};

class Pm1Adapter final {
public:
    Pm1Adapter() = default;
    Pm1Adapter(const Pm1Adapter&) = delete;
    Pm1Adapter& operator=(const Pm1Adapter&) = delete;

    // Borrows the shared bus; the adapter owns only its PM1 device state.
    [[nodiscard]] DriverResult begin(i2c_bus_handle_t bus,
                                     std::uint32_t speed_hz = 100000);
    [[nodiscard]] bool initialized() const { return initialized_; }

    // Updates only bits selected by mask and verifies the written value by readback.
    [[nodiscard]] DriverResult set_power_config(std::uint8_t mask,
                                                std::uint8_t value);
    // Selects external 5V input/output mode and verifies the BOOST bit.
    [[nodiscard]] DriverResult set_external_5v_output(bool enabled);
    // Sets the PM1-controlled status LED level and verifies the LED bit.
    [[nodiscard]] DriverResult set_status_led(bool enabled);
    // Controls the L1 3.3 V rail used by RTC, IMU and touch, with readback.
    [[nodiscard]] DriverResult set_l1_rail(bool enabled);
    // Disables L1 as the final PM1 transaction. No readback is attempted because
    // removing this rail also removes the board I2C pull-up supply.
    [[nodiscard]] DriverResult disable_l1_rail_final();
    // Reads the complete PM1 power configuration without changing it.
    [[nodiscard]] DriverResult read_power_config(std::uint8_t* config);
    // Changes the LDO hold state and confirms the hardware state by readback.
    [[nodiscard]] DriverResult set_ldo_hold(bool enabled);
    // Reads the current LDO hold state without changing it.
    [[nodiscard]] DriverResult read_ldo_hold(bool* enabled);
    // Disables the PM1 watchdog (timeout 0) and confirms the counter readback.
    [[nodiscard]] DriverResult set_watchdog_timeout(std::uint8_t timeout_sec);
    // Masks PM1 GPIO IRQs, disables wake and leaves every GPIO as a benign input.
    [[nodiscard]] DriverResult configure_all_gpio_safe();
    // Masks button/system IRQs so only explicitly selected GPIO events reach IRQ out.
    [[nodiscard]] DriverResult mask_non_gpio_irqs();
    // Routes the PM1 IRQ aggregator through GPIO1 to the ESP32 GPIO12 input.
    [[nodiscard]] DriverResult configure_irq_output();
    // Arms a normal GPIO input for falling-edge wake and unmasks its IRQ.
    [[nodiscard]] DriverResult configure_gpio_wake(Pm1Gpio gpio);
    // Disables wake and masks the IRQ without changing unrelated GPIO configuration.
    [[nodiscard]] DriverResult disable_gpio_wake(Pm1Gpio gpio);
    // Disarms wake and restores one PM1 GPIO to an unpulled GPIO input.
    [[nodiscard]] DriverResult restore_gpio_input(Pm1Gpio gpio);

    // Reads preserve the complete hardware bitmask and do not clear flags.
    [[nodiscard]] DriverResult read_wake_source(std::uint8_t* source);
    // Clears only the captured wake bits and verifies that those bits are gone.
    [[nodiscard]] DriverResult clear_wake_source(std::uint8_t captured_mask);
    // Reads the complete GPIO IRQ bitmask without clearing any pending event.
    [[nodiscard]] DriverResult read_gpio_irq(std::uint8_t* status);
    // Clears captured GPIO IRQ bits one event at a time, then verifies the result.
    [[nodiscard]] DriverResult clear_gpio_irq(std::uint8_t captured_mask);
    // Reads the complete button IRQ bitmask without clearing any pending event.
    [[nodiscard]] DriverResult read_button_irq(std::uint8_t* status);
    // Clears captured button IRQ bits one event at a time, then verifies the result.
    [[nodiscard]] DriverResult clear_button_irq(std::uint8_t captured_mask);
    // Reads the complete system IRQ bitmask without clearing any pending event.
    [[nodiscard]] DriverResult read_system_irq(std::uint8_t* status);
    // Clears captured system IRQ bits one event at a time, then verifies the result.
    [[nodiscard]] DriverResult clear_system_irq(std::uint8_t captured_mask);

    // Sets PM1 I2C idle sleep; nonzero values also enable the driver's auto-wake path.
    [[nodiscard]] DriverResult set_i2c_sleep_time(std::uint8_t seconds);
    // Sends the PM1 shutdown command; successful return means the command was accepted.
    [[nodiscard]] DriverResult shutdown();

private:
    [[nodiscard]] DriverResult result(m5pm1_err_t raw) const;
    [[nodiscard]] DriverResult verify_mask(std::uint8_t mask,
                                           std::uint8_t expected);

    M5PM1 pm1_;
    bool initialized_ = false;
};

}  // namespace m5sw::power
