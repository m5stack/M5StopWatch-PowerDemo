#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "i2c_bus.h"
#include "M5IOE1.h"

namespace m5sw::board {

inline constexpr gpio_num_t kI2cSda     = GPIO_NUM_47;
inline constexpr gpio_num_t kI2cScl     = GPIO_NUM_48;
inline constexpr gpio_num_t kKey2       = GPIO_NUM_1;
inline constexpr gpio_num_t kKey1       = GPIO_NUM_2;
inline constexpr gpio_num_t kPm1Irq     = GPIO_NUM_12;
inline constexpr gpio_num_t kTouchIrq   = GPIO_NUM_13;
inline constexpr gpio_num_t kI2sLrck    = GPIO_NUM_15;
inline constexpr gpio_num_t kI2sAdcData = GPIO_NUM_16;
inline constexpr gpio_num_t kI2sBclk    = GPIO_NUM_17;
inline constexpr gpio_num_t kI2sMclk    = GPIO_NUM_18;
inline constexpr gpio_num_t kI2sDacData = GPIO_NUM_21;

inline constexpr std::uint8_t kPm1Address        = 0x6E;
inline constexpr std::uint8_t kIoe1Address       = 0x4F;
inline constexpr std::uint8_t kRtcAddress        = 0x32;
inline constexpr std::uint8_t kImuAddress        = 0x68;
inline constexpr std::uint8_t kTouchAddress      = 0x15;
inline constexpr std::uint8_t kAudioCodecAddress = 0x18;

enum class I2cSpeed : std::uint32_t {
    standard = 100000,
    fast     = 400000,
};

enum class IoePin : std::uint8_t {
    mux_select     = static_cast<std::uint8_t>(M5IOE1_PIN_1),
    audio_enable   = static_cast<std::uint8_t>(M5IOE1_PIN_3),
    touch_reset    = static_cast<std::uint8_t>(M5IOE1_PIN_4),
    display_reset  = static_cast<std::uint8_t>(M5IOE1_PIN_5),
    l3b_enable     = static_cast<std::uint8_t>(M5IOE1_PIN_8),
    motor_pwm      = static_cast<std::uint8_t>(M5IOE1_PIN_9),
    speaker_enable = static_cast<std::uint8_t>(M5IOE1_PIN_10),
};

enum class Device : std::uint8_t {
    pm1,
    ioe1,
    rtc,
    imu,
};

using DeviceMask = std::uint8_t;

[[nodiscard]] constexpr DeviceMask device_mask(Device device)
{
    return static_cast<DeviceMask>(1U << static_cast<std::uint8_t>(device));
}

inline constexpr DeviceMask kAllProbeDevices =
    device_mask(Device::pm1) | device_mask(Device::ioe1) | device_mask(Device::rtc) | device_mask(Device::imu);

struct ProbeReport {
    bool pm1  = false;
    bool ioe1 = false;
    bool rtc  = false;
    bool imu  = false;

    // Returns true only when every device in the full board probe is present.
    [[nodiscard]] bool all_present() const;
    // Reports the presence bit for one known board device.
    [[nodiscard]] bool present(Device device) const;
    // Checks that every device selected by the mask was detected.
    [[nodiscard]] bool contains(DeviceMask required_devices) const;
};

// Creates the shared I2C bus. Repeated calls are idempotent only for the same speed.
esp_err_t initialize_i2c(I2cSpeed speed = I2cSpeed::standard);
// Deletes the shared bus after all long-lived users release their claims.
esp_err_t deinitialize_i2c();

// Borrows the shared bus and increments its lifetime claim count.
// The caller must keep the claim until all operations on the bus are complete.
esp_err_t acquire_i2c(i2c_bus_handle_t* bus);
// Releases one claim previously obtained with acquire_i2c().
esp_err_t release_i2c();

// Recreates the bus at a new speed; no outstanding claim may exist.
esp_err_t reconfigure_i2c(I2cSpeed speed);
// Returns the configured shared-bus speed.
[[nodiscard]] I2cSpeed i2c_speed();

// Scans the shared bus without clearing device state and fills the presence report.
esp_err_t probe_devices(ProbeReport* report, DeviceMask required_devices = kAllProbeDevices);

// Returns a stable human-readable label for logs and evidence records.
[[nodiscard]] const char* device_name(Device device);

}  // namespace m5sw::board
