#pragma once

#include <cstdint>

#include "esp_err.h"
#include "i2c_bus.h"

namespace m5sw::display {

inline constexpr std::uint32_t kNormalRefreshHz = 1;

struct DisplayResult {
    esp_err_t error = ESP_OK;
    int raw_error = 0;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

class DisplayController final {
public:
    DisplayController() = default;
    ~DisplayController();
    DisplayController(const DisplayController&) = delete;
    DisplayController& operator=(const DisplayController&) = delete;

    // Borrows the shared I2C bus and initializes IOE1, panel power, reset and QSPI.
    [[nodiscard]] DisplayResult begin(i2c_bus_handle_t bus,
                                      std::uint32_t i2c_speed_hz = 100000);
    // Sets panel brightness from 0 to 100 percent and records the applied value.
    [[nodiscard]] DisplayResult set_brightness_percent(std::uint8_t percent);
    // Submits one full-screen white frame through the reference framebuffer path.
    [[nodiscard]] DisplayResult show_white_static();
    // Submits one stopwatch UI frame; callers schedule this at kNormalRefreshHz.
    [[nodiscard]] DisplayResult draw_normal_frame(std::uint32_t elapsed_seconds);
    // Runs one iteration of the reference Full Load random drawing loop.
    [[nodiscard]] DisplayResult draw_full_load_step();

    // Reports whether the panel, power rail and QSPI path are initialized.
    [[nodiscard]] bool initialized() const { return initialized_; }
    // Returns the last brightness percentage requested through the adapter.
    [[nodiscard]] std::uint8_t brightness_percent() const { return brightness_percent_; }

private:
    class Impl;
    Impl* impl_ = nullptr;
    bool initialized_ = false;
    std::uint8_t brightness_percent_ = 0;
};

}  // namespace m5sw::display
