#include "m5sw_board.hpp"

#include <array>
#include <cstddef>

#include "freertos/FreeRTOS.h"

namespace m5sw::board {
namespace {

constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr std::size_t kScanCapacity = 32;

i2c_bus_handle_t g_i2c_bus = nullptr;
I2cSpeed g_i2c_speed = I2cSpeed::standard;
std::uint32_t g_i2c_claims = 0;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;

static_assert(M5IOE1_PIN_1 == 0);
static_assert(M5IOE1_PIN_2 == 1);
static_assert(M5IOE1_PIN_3 == 2);
static_assert(M5IOE1_PIN_4 == 3);
static_assert(M5IOE1_PIN_5 == 4);
static_assert(M5IOE1_PIN_6 == 5);
static_assert(M5IOE1_PIN_7 == 6);
static_assert(M5IOE1_PIN_8 == 7);
static_assert(M5IOE1_PIN_9 == 8);
static_assert(M5IOE1_PIN_10 == 9);
static_assert(M5IOE1_PIN_11 == 10);
static_assert(M5IOE1_PIN_12 == 11);
static_assert(M5IOE1_PIN_13 == 12);
static_assert(M5IOE1_PIN_14 == 13);

static_assert(static_cast<std::uint8_t>(IoePin::mux_select) == M5IOE1_PIN_1);
static_assert(static_cast<std::uint8_t>(IoePin::audio_enable) == M5IOE1_PIN_3);
static_assert(static_cast<std::uint8_t>(IoePin::touch_reset) == M5IOE1_PIN_4);
static_assert(static_cast<std::uint8_t>(IoePin::display_reset) == M5IOE1_PIN_5);
static_assert(static_cast<std::uint8_t>(IoePin::l3b_enable) == M5IOE1_PIN_8);
static_assert(static_cast<std::uint8_t>(IoePin::motor_pwm) == M5IOE1_PIN_9);
static_assert(static_cast<std::uint8_t>(IoePin::speaker_enable) == M5IOE1_PIN_10);

i2c_bus_handle_t create_bus(I2cSpeed speed)
{
    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = kI2cSda;
    config.scl_io_num = kI2cScl;
    config.sda_pullup_en = true;
    config.scl_pullup_en = true;
    config.master.clk_speed = static_cast<std::uint32_t>(speed);
    config.clk_flags = 0;
    return i2c_bus_create(kI2cPort, &config);
}

bool contains_address(const std::array<std::uint8_t, kScanCapacity>& addresses,
                      std::uint8_t count, std::uint8_t target)
{
    const std::size_t saved = count < addresses.size() ? count : addresses.size();
    for (std::size_t index = 0; index < saved; ++index) {
        if (addresses[index] == target) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool ProbeReport::all_present() const
{
    return pm1 && ioe1 && rtc && imu;
}

bool ProbeReport::present(Device device) const
{
    switch (device) {
        case Device::pm1:
            return pm1;
        case Device::ioe1:
            return ioe1;
        case Device::rtc:
            return rtc;
        case Device::imu:
            return imu;
    }
    return false;
}

bool ProbeReport::contains(DeviceMask required_devices) const
{
    constexpr std::array<Device, 4> devices = {
        Device::pm1,
        Device::ioe1,
        Device::rtc,
        Device::imu,
    };
    for (Device device : devices) {
        if ((required_devices & device_mask(device)) != 0U && !present(device)) {
            return false;
        }
    }
    return true;
}

esp_err_t initialize_i2c(I2cSpeed speed)
{
    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_bus != nullptr) {
        const bool matches = g_i2c_speed == speed;
        portEXIT_CRITICAL(&g_state_lock);
        return matches ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&g_state_lock);

    i2c_bus_handle_t bus = create_bus(speed);
    if (bus == nullptr) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_bus != nullptr) {
        portEXIT_CRITICAL(&g_state_lock);
        i2c_bus_delete(&bus);
        return ESP_ERR_INVALID_STATE;
    }
    g_i2c_bus = bus;
    g_i2c_speed = speed;
    portEXIT_CRITICAL(&g_state_lock);
    return ESP_OK;
}

esp_err_t deinitialize_i2c()
{
    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_claims != 0) {
        portEXIT_CRITICAL(&g_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    i2c_bus_handle_t bus = g_i2c_bus;
    g_i2c_bus = nullptr;
    portEXIT_CRITICAL(&g_state_lock);

    if (bus == nullptr) {
        return ESP_OK;
    }
    const esp_err_t result = i2c_bus_delete(&bus);
    if (result == ESP_OK && bus == nullptr) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&g_state_lock);
    g_i2c_bus = bus;
    portEXIT_CRITICAL(&g_state_lock);
    return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
}

esp_err_t acquire_i2c(i2c_bus_handle_t* bus)
{
    if (bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_bus == nullptr) {
        portEXIT_CRITICAL(&g_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    ++g_i2c_claims;
    *bus = g_i2c_bus;
    portEXIT_CRITICAL(&g_state_lock);
    return ESP_OK;
}

esp_err_t release_i2c()
{
    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_claims == 0) {
        portEXIT_CRITICAL(&g_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    --g_i2c_claims;
    portEXIT_CRITICAL(&g_state_lock);
    return ESP_OK;
}

esp_err_t reconfigure_i2c(I2cSpeed speed)
{
    portENTER_CRITICAL(&g_state_lock);
    if (g_i2c_claims != 0) {
        portEXIT_CRITICAL(&g_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const I2cSpeed previous_speed = g_i2c_speed;
    i2c_bus_handle_t previous_bus = g_i2c_bus;
    g_i2c_bus = nullptr;
    portEXIT_CRITICAL(&g_state_lock);

    if (previous_bus != nullptr) {
        const esp_err_t delete_result = i2c_bus_delete(&previous_bus);
        if (delete_result != ESP_OK || previous_bus != nullptr) {
            portENTER_CRITICAL(&g_state_lock);
            g_i2c_bus = previous_bus;
            portEXIT_CRITICAL(&g_state_lock);
            return delete_result == ESP_OK ? ESP_ERR_INVALID_STATE : delete_result;
        }
    }

    i2c_bus_handle_t replacement = create_bus(speed);
    if (replacement == nullptr) {
        replacement = create_bus(previous_speed);
        portENTER_CRITICAL(&g_state_lock);
        g_i2c_bus = replacement;
        g_i2c_speed = previous_speed;
        portEXIT_CRITICAL(&g_state_lock);
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&g_state_lock);
    g_i2c_bus = replacement;
    g_i2c_speed = speed;
    portEXIT_CRITICAL(&g_state_lock);
    return ESP_OK;
}

I2cSpeed i2c_speed()
{
    portENTER_CRITICAL(&g_state_lock);
    const I2cSpeed speed = g_i2c_speed;
    portEXIT_CRITICAL(&g_state_lock);
    return speed;
}

esp_err_t probe_devices(ProbeReport* report, DeviceMask required_devices)
{
    if (report == nullptr || required_devices == 0U ||
        (required_devices & static_cast<DeviceMask>(~kAllProbeDevices)) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    *report = {};

    i2c_bus_handle_t bus = nullptr;
    esp_err_t result = acquire_i2c(&bus);
    if (result != ESP_OK) {
        return result;
    }

    std::array<std::uint8_t, kScanCapacity> addresses = {};
    const std::uint8_t found =
        i2c_bus_scan(bus, addresses.data(), static_cast<std::uint8_t>(addresses.size()));
    const esp_err_t release_result = release_i2c();
    if (release_result != ESP_OK) {
        return release_result;
    }

    report->pm1 = contains_address(addresses, found, kPm1Address);
    report->ioe1 = contains_address(addresses, found, kIoe1Address);
    report->rtc = contains_address(addresses, found, kRtcAddress);
    report->imu = contains_address(addresses, found, kImuAddress);

    return report->contains(required_devices) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

const char* device_name(Device device)
{
    switch (device) {
        case Device::pm1:
            return "M5PM1 (0x6E)";
        case Device::ioe1:
            return "M5IOE1 (0x4F)";
        case Device::rtc:
            return "RX8130 (0x32)";
        case Device::imu:
            return "BMI270 (0x68)";
    }
    return "unknown";
}

}  // namespace m5sw::board
