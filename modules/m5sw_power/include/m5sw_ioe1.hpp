#pragma once

#include <cstdint>

#include "M5IOE1.h"
#include "esp_err.h"
#include "i2c_bus.h"
#include "m5sw_board.hpp"

namespace m5sw::power {

struct Ioe1Result {
    esp_err_t error = ESP_OK;
    int raw_error = M5IOE1_OK;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

class Ioe1Adapter final {
public:
    Ioe1Adapter() = default;
    Ioe1Adapter(const Ioe1Adapter&) = delete;
    Ioe1Adapter& operator=(const Ioe1Adapter&) = delete;

    // Borrows the shared bus and initializes IOE1 at the board address 0x4F.
    [[nodiscard]] Ioe1Result begin(i2c_bus_handle_t bus,
                                   std::uint32_t speed_hz = 100000);
    [[nodiscard]] bool initialized() const { return initialized_; }

    // Clears all IOE1 state and waits until the reset device responds at 100 kHz.
    // The adapter must be begun again before use.
    [[nodiscard]] Ioe1Result factory_reset();

    // Applies the typed IOE1 sleep, speed, wake-edge and pull configuration, then reads it back.
    [[nodiscard]] Ioe1Result configure_i2c(bool sleep_enabled,
                                            std::uint32_t speed_hz);
    // Drives a board-mapped output pin and leaves it configured as an output.
    [[nodiscard]] Ioe1Result set_pin(m5sw::board::IoePin pin, bool high);
    // Reads the cached output state; this is not a physical input sampling operation.
    [[nodiscard]] Ioe1Result read_pin(m5sw::board::IoePin pin, bool* high);
    // Sets the shared PWM clock and verifies the applied frequency.
    [[nodiscard]] Ioe1Result set_pwm_frequency(std::uint16_t frequency_hz);
    // Sets PWM channel 0 using a 0-100 percent duty value and verifies the quantized result.
    [[nodiscard]] Ioe1Result set_motor_percent(std::uint8_t percent);
    // Disables all PWM channels before IOE1 idle sleep or low-power entry.
    [[nodiscard]] Ioe1Result stop_all_pwm();
    // Sets IOE1 idle sleep and confirms the stored timeout by readback.
    [[nodiscard]] Ioe1Result set_idle_sleep(std::uint8_t sleep_time);
    // Verifies that the first I2C transaction after idle sleep auto-wakes IOE1.
    [[nodiscard]] Ioe1Result probe_idle_sleep();

private:
    [[nodiscard]] Ioe1Result result(m5ioe1_err_t raw) const;
    [[nodiscard]] Ioe1Result verify_i2c_config(bool sleep_enabled,
                                               std::uint32_t speed_hz);

    M5IOE1 ioe1_;
    i2c_bus_handle_t bus_ = nullptr;
    bool initialized_ = false;
};

}  // namespace m5sw::power
