#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_sleep.h"
#include "i2c_bus.h"
#include "m5sw_ioe1.hpp"
#include "m5sw_pm1.hpp"

namespace m5sw::power {

struct PowerResult {
    esp_err_t error = ESP_OK;
    int raw_error = 0;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

struct AdapterProbeResult {
    esp_err_t error = ESP_OK;
    const char* stage = "complete";
    int raw_error = 0;

    [[nodiscard]] bool ok() const { return error == ESP_OK; }
};

// Runs a non-destructive PM1/IOE1 adapter probe and restores safe GPIO and
// power settings before returning.
[[nodiscard]] AdapterProbeResult run_adapter_probe(i2c_bus_handle_t bus);

enum class WakeSelection : std::uint8_t {
    none,
    rtc_imu,
    port,
};

enum class L2Pm1IdleSleep : std::uint8_t {
    enabled,
    disabled,
};

class PowerController final {
public:
    PowerController() = default;
    PowerController(const PowerController&) = delete;
    PowerController& operator=(const PowerController&) = delete;

    // Borrows the board I2C bus and initializes the PM1 power adapter.
    [[nodiscard]] PowerResult begin(i2c_bus_handle_t bus,
                                    std::uint32_t speed_hz = 100000);
    // Applies the L0 configuration; no RTC/IMU or IOE1 state is retained.
    [[nodiscard]] PowerResult prepare_l0();
    // Retains the LDO/RTC/IMU path and arms only the selected PM1 wake line.
    [[nodiscard]] PowerResult prepare_l1(
        WakeSelection wake = WakeSelection::none);
    // Applies the active L2 configuration and arms only the selected wake source.
    // I2C idle sleep is intentionally deferred until enter_l2().
    [[nodiscard]] PowerResult prepare_l2(WakeSelection wake);
    // Applies the wake-free L2 peripheral baseline and enables PM1/IOE1 I2C
    // idle sleep, but deliberately leaves the ESP32 running.
    [[nodiscard]] PowerResult prepare_l2_active();
    // Prepares L0 and issues the PM1 shutdown command; it normally does not return.
    [[nodiscard]] PowerResult enter_l0();
    // Uses an existing matching L1 preparation when available, then issues the
    // PM1 shutdown command; it normally does not return.
    [[nodiscard]] PowerResult enter_l1(
        WakeSelection wake = WakeSelection::none);
    // Finalizes IOE1 idle sleep and, unless explicitly disabled for the short
    // RX8130 timer pulse, PM1 idle sleep. It then waits for the reference
    // settling interval and enters ESP32 Deep Sleep without another transaction.
    [[nodiscard]] PowerResult enter_l2(
        WakeSelection wake,
        L2Pm1IdleSleep pm1_idle_sleep = L2Pm1IdleSleep::enabled);

private:
    [[nodiscard]] PowerResult from_pm1(DriverResult result) const;
    [[nodiscard]] PowerResult from_ioe1(Ioe1Result result) const;
    [[nodiscard]] PowerResult begin_ioe1();
    [[nodiscard]] PowerResult configure_l2_wake(WakeSelection wake);
    [[nodiscard]] PowerResult finalize_l2_peripherals(
        L2Pm1IdleSleep pm1_idle_sleep);

    i2c_bus_handle_t bus_ = nullptr;
    Pm1Adapter pm1_;
    Ioe1Adapter ioe1_;
    bool initialized_ = false;
    bool ioe1_initialized_ = false;
    bool l1_prepared_ = false;
    WakeSelection l1_prepared_wake_ = WakeSelection::none;
    bool l2_prepared_ = false;
    WakeSelection l2_prepared_wake_ = WakeSelection::none;
};

}  // namespace m5sw::power
