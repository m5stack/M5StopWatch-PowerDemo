#include "m5sw_ioe1.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace m5sw::power {
namespace {

esp_err_t map_error(m5ioe1_err_t error)
{
    switch (error) {
        case M5IOE1_OK:
            return ESP_OK;
        case M5IOE1_ERR_INVALID_ARG:
            return ESP_ERR_INVALID_ARG;
        case M5IOE1_ERR_NOT_INIT:
            return ESP_ERR_INVALID_STATE;
        case M5IOE1_ERR_NOT_SUPPORTED:
            return ESP_ERR_NOT_SUPPORTED;
        case M5IOE1_ERR_TIMEOUT:
            return ESP_ERR_TIMEOUT;
        default:
            return ESP_FAIL;
    }
}

m5ioe1_i2c_speed_t speed_enum(std::uint32_t speed_hz)
{
    return speed_hz == 400000U ? M5IOE1_I2C_SPEED_400K : M5IOE1_I2C_SPEED_100K;
}

constexpr TickType_t kResetReadyTimeout = pdMS_TO_TICKS(1200);
constexpr TickType_t kResetPollInterval = pdMS_TO_TICKS(20);

}  // namespace

Ioe1Result Ioe1Adapter::result(m5ioe1_err_t raw) const
{
    return Ioe1Result{map_error(raw), static_cast<int>(raw)};
}

Ioe1Result Ioe1Adapter::begin(i2c_bus_handle_t bus, std::uint32_t speed_hz)
{
    if (bus == nullptr || (speed_hz != 100000U && speed_hz != 400000U)) {
        return Ioe1Result{ESP_ERR_INVALID_ARG, M5IOE1_ERR_INVALID_ARG};
    }
    const m5ioe1_err_t raw = ioe1_.begin(bus, m5sw::board::kIoe1Address,
                                         speed_hz, M5IOE1_INT_MODE_DISABLED);
    const Ioe1Result mapped = result(raw);
    initialized_ = mapped.ok();
    if (mapped.ok()) bus_ = bus;
    return mapped;
}

Ioe1Result Ioe1Adapter::factory_reset()
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    const Ioe1Result reset = result(ioe1_.factoryReset());
    if (reset.ok()) {
        // factoryReset() invalidates the driver's hardware and software state.
        initialized_ = false;
        i2c_bus_device_handle_t ready_device = i2c_bus_device_create(
            bus_, m5sw::board::kIoe1Address, 100000);
        if (ready_device == nullptr) return result(M5IOE1_FAIL);

        const TickType_t start = xTaskGetTickCount();
        bool ready = false;
        do {
            std::uint8_t uid[2] = {};
            ready = i2c_bus_read_bytes(ready_device, M5IOE1_REG_UID_L,
                                       sizeof(uid), uid) == ESP_OK;
            if (!ready) vTaskDelay(kResetPollInterval);
        } while (!ready && (xTaskGetTickCount() - start) < kResetReadyTimeout);

        const esp_err_t delete_result = i2c_bus_device_delete(&ready_device);
        if (delete_result != ESP_OK) return result(M5IOE1_FAIL);
        if (!ready) return result(M5IOE1_ERR_TIMEOUT);
    }
    return reset;
}

Ioe1Result Ioe1Adapter::configure_i2c(bool sleep_enabled, std::uint32_t speed_hz)
{
    if (!initialized_ || (speed_hz != 100000U && speed_hz != 400000U)) {
        return Ioe1Result{!initialized_ ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG,
                          !initialized_ ? M5IOE1_ERR_NOT_INIT : M5IOE1_ERR_INVALID_ARG};
    }
    const std::uint8_t sleep_time = sleep_enabled ? 1U : 0U;
    const Ioe1Result written = result(ioe1_.setI2cConfig(
        sleep_time, speed_enum(speed_hz), M5IOE1_WAKE_EDGE_RISING,
        M5IOE1_PULL_DISABLED));
    if (!written.ok()) return written;
    return verify_i2c_config(sleep_enabled, speed_hz);
}

Ioe1Result Ioe1Adapter::verify_i2c_config(bool sleep_enabled, std::uint32_t speed_hz)
{
    std::uint8_t sleep_time = 0;
    m5ioe1_i2c_speed_t actual_speed = M5IOE1_I2C_SPEED_100K;
    m5ioe1_wake_edge_t actual_edge = M5IOE1_WAKE_EDGE_FALLING;
    m5ioe1_pull_config_t actual_pull = M5IOE1_PULL_ENABLED;
    Ioe1Result read = result(ioe1_.getI2cSleepTime(&sleep_time));
    if (!read.ok()) return read;
    read = result(ioe1_.getI2cSpeed(&actual_speed));
    if (!read.ok()) return read;
    read = result(ioe1_.getI2cWakeEdge(&actual_edge));
    if (!read.ok()) return read;
    read = result(ioe1_.getI2cPullConfig(&actual_pull));
    if (!read.ok()) return read;
    const bool matches = (sleep_time == (sleep_enabled ? 1U : 0U)) &&
                         (actual_speed == speed_enum(speed_hz)) &&
                         actual_edge == M5IOE1_WAKE_EDGE_RISING &&
                         actual_pull == M5IOE1_PULL_DISABLED;
    return matches ? Ioe1Result{}
                   : Ioe1Result{ESP_ERR_INVALID_RESPONSE, M5IOE1_FAIL};
}

Ioe1Result Ioe1Adapter::set_pin(m5sw::board::IoePin pin, bool high)
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    const auto raw_pin = static_cast<std::uint8_t>(pin);
    m5ioe1_err_t mode_error = M5IOE1_OK;
    ioe1_.pinModeWithRes(raw_pin, OUTPUT, &mode_error);
    if (mode_error != M5IOE1_OK) return result(mode_error);
    m5ioe1_err_t write_error = M5IOE1_OK;
    ioe1_.digitalWriteWithRes(raw_pin, high ? HIGH : LOW, &write_error);
    return result(write_error);
}

Ioe1Result Ioe1Adapter::read_pin(m5sw::board::IoePin pin, bool* high)
{
    if (!initialized_ || high == nullptr) {
        return result(high == nullptr ? M5IOE1_ERR_INVALID_ARG : M5IOE1_ERR_NOT_INIT);
    }
    bool is_output = false;
    std::uint8_t level = 0;
    std::uint8_t pull = 0;
    const Ioe1Result read = result(ioe1_.getCachedPinState(
        static_cast<std::uint8_t>(pin), &is_output, &level, &pull));
    if (!read.ok()) return read;
    (void)pull;
    *high = level != 0U;
    return is_output ? Ioe1Result{} :
                       Ioe1Result{ESP_ERR_INVALID_RESPONSE, M5IOE1_FAIL};
}

Ioe1Result Ioe1Adapter::set_pwm_frequency(std::uint16_t frequency_hz)
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    if (frequency_hz == 0U) return result(M5IOE1_ERR_INVALID_ARG);
    Ioe1Result written = result(ioe1_.setPwmFrequency(frequency_hz));
    if (!written.ok()) return written;
    std::uint16_t actual = 0;
    written = result(ioe1_.getPwmFrequency(&actual));
    if (!written.ok()) return written;
    return actual == frequency_hz
               ? Ioe1Result{}
               : Ioe1Result{ESP_ERR_INVALID_RESPONSE, M5IOE1_FAIL};
}

Ioe1Result Ioe1Adapter::set_motor_percent(std::uint8_t percent)
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    if (percent > 100U) return result(M5IOE1_ERR_INVALID_ARG);
    const bool enable = percent != 0U;
    Ioe1Result written = result(ioe1_.setPwmDuty(0, percent, false, enable));
    if (!written.ok()) return written;
    std::uint8_t actual = 0;
    bool polarity = false;
    bool actual_enabled = false;
    written = result(ioe1_.getPwmDuty(0, &actual, &polarity, &actual_enabled));
    if (!written.ok()) return written;
    const std::uint8_t delta = actual > percent ? actual - percent : percent - actual;
    // The public percentage getter truncates duty12 * 100 / 4095.
    return delta <= 1U && polarity == false && actual_enabled == enable
               ? Ioe1Result{}
               : Ioe1Result{ESP_ERR_INVALID_RESPONSE, M5IOE1_FAIL};
}

Ioe1Result Ioe1Adapter::stop_all_pwm()
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    for (std::uint8_t channel = 0; channel < 4; ++channel) {
        const Ioe1Result stopped = result(ioe1_.setPwmDuty(channel, 0, false, false));
        if (!stopped.ok()) return stopped;
    }
    return set_motor_percent(0);
}

Ioe1Result Ioe1Adapter::set_idle_sleep(std::uint8_t sleep_time)
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    const Ioe1Result written = result(ioe1_.setI2cSleepTime(sleep_time));
    if (!written.ok()) return written;
    std::uint8_t actual = 0;
    const Ioe1Result read = result(ioe1_.getI2cSleepTime(&actual));
    if (!read.ok()) return read;
    return actual == sleep_time ? Ioe1Result{}
                                : Ioe1Result{ESP_ERR_INVALID_RESPONSE,
                                             M5IOE1_FAIL};
}

Ioe1Result Ioe1Adapter::probe_idle_sleep()
{
    if (!initialized_) return result(M5IOE1_ERR_NOT_INIT);
    Ioe1Result configured = set_idle_sleep(1);
    if (!configured.ok()) return configured;

    // The public driver emits its auto-wake start condition after one second
    // of inactivity. The read below is the first transaction after that idle
    // interval and must succeed without an explicit caller wake command.
    vTaskDelay(pdMS_TO_TICKS(1100));
    std::uint8_t sleep_time = 0;
    Ioe1Result awakened = result(ioe1_.getI2cSleepTime(&sleep_time));
    if (!awakened.ok()) return awakened;
    if (sleep_time != 1U) {
        return {ESP_ERR_INVALID_RESPONSE, M5IOE1_FAIL};
    }
    return set_idle_sleep(0);
}

}  // namespace m5sw::power
