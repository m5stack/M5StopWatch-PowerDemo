#pragma once

#include <cstdint>
#include <cstddef>
#include <ctime>

#include "esp_err.h"
#include "i2c_bus.h"
#include "rx8130.h"

namespace m5sw::wakeup {

inline constexpr std::uint8_t kRx8130Address        = 0x32;
inline constexpr std::uint16_t kRtcTimerSeconds     = 120;
inline constexpr std::uint8_t kRtcAlarmDelaySeconds = 120;

enum class RtcEvent : std::uint8_t {
    none   = 0,
    alarm  = 1U << 0,
    timer  = 1U << 1,
    update = 1U << 2,
};

struct RtcFlags {
    // Raw RX8130 FLAG register value. These bits represent latched events,
    // not interrupt-enable configuration.
    std::uint8_t raw = 0;

    // Returns true when the RX8130 calendar alarm event is latched (FLAG bit 3).
    [[nodiscard]] bool alarm() const
    {
        return (raw & (1U << 3)) != 0U;
    }
    // Returns true when the RX8130 countdown timer event is latched (FLAG bit 4).
    [[nodiscard]] bool timer() const
    {
        return (raw & (1U << 4)) != 0U;
    }
    // Returns true when the RX8130 periodic update event is latched (FLAG bit 5).
    [[nodiscard]] bool update() const
    {
        return (raw & (1U << 5)) != 0U;
    }
    // Converts the latched FLAG bits to the type-safe event mask used by wake logic.
    [[nodiscard]] RtcEvent events() const;
};

struct RtcWakeupResult {
    esp_err_t error = ESP_OK;
    int raw_error   = 0;

    [[nodiscard]] bool ok() const
    {
        return error == ESP_OK;
    }
};

class RtcWakeup final {
public:
    RtcWakeup() = default;
    ~RtcWakeup();

    RtcWakeup(const RtcWakeup&)            = delete;
    RtcWakeup& operator=(const RtcWakeup&) = delete;

    // Creates the raw device handle and probes RX8130 before marking the adapter ready.
    [[nodiscard]] RtcWakeupResult begin(i2c_bus_handle_t bus, std::uint32_t speed_hz = 100000);
    // Reports whether the raw RX8130 handle and calendar driver are ready.
    [[nodiscard]] bool initialized() const
    {
        return initialized_;
    }

    // The baseline must be a valid UTC-like calendar value with tm_sec == 0.
    // The driver stores calendar fields only; timezone conversion is out of scope.
    [[nodiscard]] RtcWakeupResult set_known_time(const std::tm& baseline);
    // Reads and validates the seven calendar registers without changing RTC state.
    [[nodiscard]] RtcWakeupResult read_time(std::tm* time) const;

    // Configure a one-shot 10 second timer and clear stale RTC flags first.
    [[nodiscard]] RtcWakeupResult configure_timer_10s();
    // Configure the formal one-shot 120 second timer and verify its counter.
    [[nodiscard]] RtcWakeupResult configure_timer_120s();

    // Set the clock to baseline and arm the next normalized minute alarm.
    // With tm_sec == 0 this produces an exact 60 second interval.
    [[nodiscard]] RtcWakeupResult configure_alarm_60s(const std::tm& baseline, std::tm* target);
    // Set the clock to baseline and arm an alarm exactly two normalized minutes later.
    [[nodiscard]] RtcWakeupResult configure_alarm_120s(const std::tm& baseline, std::tm* target);

    // Captures the complete RTC flag register without clearing any event.
    [[nodiscard]] RtcWakeupResult read_flags(RtcFlags* flags) const;
    // Clears only the events represented by captured and verifies the remaining bits.
    [[nodiscard]] RtcWakeupResult clear_flags(RtcFlags captured);
    // Disables alarm and timer interrupt enables and verifies both control registers.
    [[nodiscard]] RtcWakeupResult disable_interrupts();

    // Pure calendar helper used by boundary tests and alarm setup.
    [[nodiscard]] static bool normalize_next_minute(const std::tm& current, std::tm* target);

private:
    [[nodiscard]] RtcWakeupResult raw_read(std::uint8_t reg, std::uint8_t* data, std::size_t length) const;
    [[nodiscard]] RtcWakeupResult raw_write(std::uint8_t reg, const std::uint8_t* data, std::size_t length);
    [[nodiscard]] RtcWakeupResult verify_time(const std::tm& expected) const;
    [[nodiscard]] RtcWakeupResult verify_flag_bits(std::uint8_t expected_clear) const;
    [[nodiscard]] RtcWakeupResult configure_timer_seconds(std::uint16_t seconds);
    [[nodiscard]] RtcWakeupResult configure_alarm_minutes(const std::tm& baseline, std::uint8_t delay_minutes,
                                                          std::tm* target);

    i2c_bus_handle_t bus_               = nullptr;
    i2c_bus_device_handle_t raw_device_ = nullptr;
    RX8130_Class rtc_;
    bool initialized_ = false;
};

}  // namespace m5sw::wakeup
