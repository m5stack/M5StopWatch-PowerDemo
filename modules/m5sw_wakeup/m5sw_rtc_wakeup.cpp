#include "m5sw_rtc_wakeup.hpp"

#include <array>
#include <cstddef>

namespace m5sw::wakeup {
namespace {

constexpr std::uint8_t kRegSeconds         = 0x10;
constexpr std::uint8_t kRegAlarmMinute     = 0x17;
constexpr std::uint8_t kRegTimerCounterLow = 0x1A;
constexpr std::uint8_t kRegExtension       = 0x1C;
constexpr std::uint8_t kRegFlag            = 0x1D;
constexpr std::uint8_t kRegControl0        = 0x1E;

constexpr std::uint8_t kFlagAlarm                  = 1U << 3;
constexpr std::uint8_t kFlagTimer                  = 1U << 4;
constexpr std::uint8_t kFlagUpdate                 = 1U << 5;
constexpr std::uint8_t kExtensionTimerEnable       = 1U << 4;
constexpr std::uint8_t kExtensionTimerSelectMask   = 3U << 0;
constexpr std::uint8_t kControlAlarmEnable         = 1U << 3;
constexpr std::uint8_t kControlTimerEnable         = 1U << 4;
constexpr std::uint8_t kControlUpdateEnable        = 1U << 5;
constexpr std::uint8_t kControlInterruptEnableMask = kControlAlarmEnable | kControlTimerEnable | kControlUpdateEnable;

bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month)
{
    constexpr std::array<int, 12> kDays = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month < 0 || month >= static_cast<int>(kDays.size())) {
        return 0;
    }
    if (month == 1 && is_leap_year(year)) {
        return 29;
    }
    return kDays[static_cast<std::size_t>(month)];
}

bool valid_calendar(const std::tm& value)
{
    const int year = value.tm_year + 1900;
    return year >= 2000 && year <= 2099 && value.tm_wday >= 0 && value.tm_wday <= 6 && value.tm_sec >= 0 &&
           value.tm_sec <= 59 && value.tm_min >= 0 && value.tm_min <= 59 && value.tm_hour >= 0 && value.tm_hour <= 23 &&
           value.tm_mon >= 0 && value.tm_mon <= 11 && value.tm_mday >= 1 &&
           value.tm_mday <= days_in_month(year, value.tm_mon);
}

std::uint8_t dec_to_bcd(int value)
{
    return static_cast<std::uint8_t>(((value / 10) << 4) | (value % 10));
}

int bcd_to_dec(std::uint8_t value)
{
    return static_cast<int>((value >> 4) * 10U + (value & 0x0FU));
}

bool decode_time(const std::uint8_t* data, std::tm* time)
{
    if (time == nullptr) {
        return false;
    }
    const int month              = bcd_to_dec(data[5] & 0x1F) - 1;
    const int day                = bcd_to_dec(data[4] & 0x3F);
    const int year               = 2000 + bcd_to_dec(data[6]);
    time->tm_sec                 = bcd_to_dec(data[0] & 0x7F);
    time->tm_min                 = bcd_to_dec(data[1] & 0x7F);
    time->tm_hour                = bcd_to_dec(data[2] & 0x3F);
    time->tm_mday                = day;
    time->tm_mon                 = month;
    time->tm_year                = year - 1900;
    time->tm_isdst               = 0;
    time->tm_wday                = 0;
    const std::uint8_t wday_mask = data[3] & 0x7F;
    for (int index = 0; index < 7; ++index) {
        if ((wday_mask & (1U << index)) != 0U) {
            time->tm_wday = index;
            break;
        }
    }
    return valid_calendar(*time);
}

}  // namespace

RtcEvent RtcFlags::events() const
{
    RtcEvent result = RtcEvent::none;
    if (alarm())
        result = static_cast<RtcEvent>(static_cast<std::uint8_t>(result) | static_cast<std::uint8_t>(RtcEvent::alarm));
    if (timer())
        result = static_cast<RtcEvent>(static_cast<std::uint8_t>(result) | static_cast<std::uint8_t>(RtcEvent::timer));
    if (update())
        result = static_cast<RtcEvent>(static_cast<std::uint8_t>(result) | static_cast<std::uint8_t>(RtcEvent::update));
    return result;
}

RtcWakeup::~RtcWakeup()
{
    if (raw_device_ != nullptr) {
        i2c_bus_device_delete(&raw_device_);
    }
}

RtcWakeupResult RtcWakeup::begin(i2c_bus_handle_t bus, std::uint32_t speed_hz)
{
    if (bus == nullptr || speed_hz == 0U) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }
    if (initialized_) {
        return bus_ == bus ? RtcWakeupResult{} : RtcWakeupResult{ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    }

    raw_device_ = i2c_bus_device_create(bus, kRx8130Address, speed_hz);
    if (raw_device_ == nullptr) {
        return {ESP_ERR_NO_MEM, ESP_ERR_NO_MEM};
    }

    if (!rtc_.begin(bus, kRx8130Address)) {
        i2c_bus_device_delete(&raw_device_);
        return {ESP_FAIL, ESP_FAIL};
    }

    bus_                         = bus;
    initialized_                 = true;
    std::uint8_t probe           = 0;
    const RtcWakeupResult result = raw_read(kRegSeconds, &probe, 1);
    if (!result.ok()) {
        initialized_ = false;
        bus_         = nullptr;
        i2c_bus_device_delete(&raw_device_);
        return result;
    }
    return {};
}

RtcWakeupResult RtcWakeup::raw_read(std::uint8_t reg, std::uint8_t* data, std::size_t length) const
{
    if (!initialized_ || raw_device_ == nullptr || data == nullptr || length == 0U) {
        return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    }
    const esp_err_t result = i2c_bus_read_bytes(raw_device_, reg, length, data);
    return {result, result};
}

RtcWakeupResult RtcWakeup::raw_write(std::uint8_t reg, const std::uint8_t* data, std::size_t length)
{
    if (!initialized_ || raw_device_ == nullptr || data == nullptr || length == 0U) {
        return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    }
    const esp_err_t result = i2c_bus_write_bytes(raw_device_, reg, length, data);
    return {result, result};
}

RtcWakeupResult RtcWakeup::set_known_time(const std::tm& baseline)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (!valid_calendar(baseline) || baseline.tm_sec != 0) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    if (!rtc_.begin(bus_, kRx8130Address)) {
        return {ESP_FAIL, ESP_FAIL};
    }
    std::tm copy = baseline;
    rtc_.setTime(&copy);
    return verify_time(baseline);
}

RtcWakeupResult RtcWakeup::read_time(std::tm* time) const
{
    if (time == nullptr) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    std::array<std::uint8_t, 7> data = {};
    const RtcWakeupResult result     = raw_read(kRegSeconds, data.data(), data.size());
    if (!result.ok()) return result;
    return decode_time(data.data(), time) ? RtcWakeupResult{}
                                          : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

RtcWakeupResult RtcWakeup::verify_time(const std::tm& expected) const
{
    std::tm actual               = {};
    const RtcWakeupResult result = read_time(&actual);
    if (!result.ok()) return result;
    const bool same = actual.tm_sec == expected.tm_sec && actual.tm_min == expected.tm_min &&
                      actual.tm_hour == expected.tm_hour && actual.tm_mday == expected.tm_mday &&
                      actual.tm_mon == expected.tm_mon && actual.tm_year == expected.tm_year &&
                      actual.tm_wday == expected.tm_wday;
    return same ? RtcWakeupResult{} : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

RtcWakeupResult RtcWakeup::configure_timer_10s()
{
    return configure_timer_seconds(10);
}

RtcWakeupResult RtcWakeup::configure_timer_120s()
{
    return configure_timer_seconds(kRtcTimerSeconds);
}

RtcWakeupResult RtcWakeup::configure_timer_seconds(std::uint16_t seconds)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (seconds == 0U) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};

    // Clear stale wake flags, reinitialize the RX8130, and arm the 1 Hz countdown.
    // Register readback below keeps a successful API call from being mistaken for
    // a valid timer when the device did not accept one of the control writes.
    const std::uint8_t clear = 0;
    RtcWakeupResult result   = raw_write(kRegFlag, &clear, 1);
    if (!result.ok()) return result;

    if (!rtc_.begin(bus_, kRx8130Address)) return {ESP_FAIL, ESP_FAIL};
    rtc_.disableIrq();
    rtc_.setTimerIrq(seconds);

    std::uint8_t extension = 0;
    result                 = raw_read(kRegExtension, &extension, 1);
    if (!result.ok()) return result;
    std::uint8_t control = 0;
    result               = raw_read(kRegControl0, &control, 1);
    if (!result.ok()) return result;
    const bool timer_ok = (extension & kExtensionTimerEnable) != 0U && (extension & kExtensionTimerSelectMask) == 2U &&
                          (control & kControlTimerEnable) != 0U;
    if (!timer_ok) return {ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};

    std::uint8_t counter[2] = {};
    result                  = raw_read(kRegTimerCounterLow, counter, sizeof(counter));
    if (!result.ok()) return result;
    const std::uint16_t count = static_cast<std::uint16_t>(counter[0]) | (static_cast<std::uint16_t>(counter[1]) << 8);
    return count == seconds ? RtcWakeupResult{} : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

bool RtcWakeup::normalize_next_minute(const std::tm& current, std::tm* target)
{
    if (target == nullptr || !valid_calendar(current)) return false;

    // Build the next whole-minute calendar value explicitly so month, year, and
    // weekday rollovers remain correct at the end of a month or year.
    *target        = current;
    target->tm_sec = 0;
    ++target->tm_min;
    if (target->tm_min < 60) return true;
    target->tm_min = 0;
    ++target->tm_hour;
    if (target->tm_hour < 24) return true;
    target->tm_hour = 0;
    ++target->tm_mday;
    target->tm_wday = (target->tm_wday + 1) % 7;
    const int year  = target->tm_year + 1900;
    if (target->tm_mday <= days_in_month(year, target->tm_mon)) return true;
    target->tm_mday = 1;
    ++target->tm_mon;
    if (target->tm_mon < 12) return true;
    target->tm_mon = 0;
    ++target->tm_year;
    return true;
}

RtcWakeupResult RtcWakeup::configure_alarm_60s(const std::tm& baseline, std::tm* target)
{
    return configure_alarm_minutes(baseline, 1, target);
}

RtcWakeupResult RtcWakeup::configure_alarm_120s(const std::tm& baseline, std::tm* target)
{
    return configure_alarm_minutes(baseline, 2, target);
}

RtcWakeupResult RtcWakeup::configure_alarm_minutes(const std::tm& baseline, std::uint8_t delay_minutes, std::tm* target)
{
    if (!initialized_) {
        return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    }
    if (target == nullptr) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }
    if (!valid_calendar(baseline) || baseline.tm_sec != 0 || delay_minutes == 0U) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    // Normalize the requested delay one minute at a time before touching the RTC.
    // This preserves calendar carry rules and gives callers the exact target.
    std::tm normalized = baseline;
    for (std::uint8_t minute = 0; minute < delay_minutes; ++minute) {
        std::tm next = {};
        if (!normalize_next_minute(normalized, &next)) {
            return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
        }
        normalized = next;
    }
    *target = normalized;

    // Program the known baseline first, then disable unrelated IRQ sources before
    // arming the alarm. Readback verifies both the minute match and enable bit.
    RtcWakeupResult result = set_known_time(baseline);
    if (!result.ok()) return result;

    if (!rtc_.begin(bus_, kRx8130Address)) return {ESP_FAIL, ESP_FAIL};
    result = disable_interrupts();
    if (!result.ok()) return result;
    rtc_.setAlarmIrq(target);

    std::uint8_t minute = 0;
    result              = raw_read(kRegAlarmMinute, &minute, 1);
    if (!result.ok()) return result;
    std::uint8_t control = 0;
    result               = raw_read(kRegControl0, &control, 1);
    if (!result.ok()) return result;
    const bool alarm_ok = (minute & 0x7F) == dec_to_bcd(target->tm_min) && (control & kControlAlarmEnable) != 0U;
    return alarm_ok ? RtcWakeupResult{} : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

RtcWakeupResult RtcWakeup::read_flags(RtcFlags* flags) const
{
    if (flags == nullptr) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    return raw_read(kRegFlag, &flags->raw, 1);
}

RtcWakeupResult RtcWakeup::verify_flag_bits(std::uint8_t expected_clear) const
{
    RtcFlags flags;
    const RtcWakeupResult result = read_flags(&flags);
    if (!result.ok()) return result;
    return (flags.raw & expected_clear) == 0U ? RtcWakeupResult{}
                                              : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

RtcWakeupResult RtcWakeup::clear_flags(RtcFlags captured)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    const std::uint8_t mask = captured.raw & (kFlagAlarm | kFlagTimer | kFlagUpdate);
    if (mask == 0U) return {};
    std::uint8_t current   = 0;
    RtcWakeupResult result = raw_read(kRegFlag, &current, 1);
    if (!result.ok()) return result;
    current = static_cast<std::uint8_t>(current & ~mask);
    result  = raw_write(kRegFlag, &current, 1);
    if (!result.ok()) return result;
    return verify_flag_bits(mask);
}

RtcWakeupResult RtcWakeup::disable_interrupts()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    std::uint8_t control   = 0;
    RtcWakeupResult result = raw_read(kRegControl0, &control, 1);
    if (!result.ok()) return result;
    control = static_cast<std::uint8_t>(control & ~kControlInterruptEnableMask);
    result  = raw_write(kRegControl0, &control, 1);
    if (!result.ok()) return result;
    std::uint8_t extension = 0;
    result                 = raw_read(kRegExtension, &extension, 1);
    if (!result.ok()) return result;
    extension = static_cast<std::uint8_t>(extension & ~kExtensionTimerEnable);
    result    = raw_write(kRegExtension, &extension, 1);
    if (!result.ok()) return result;
    std::uint8_t verify_control = 0;
    result                      = raw_read(kRegControl0, &verify_control, 1);
    if (!result.ok()) return result;
    std::uint8_t verify_extension = 0;
    result                        = raw_read(kRegExtension, &verify_extension, 1);
    if (!result.ok()) return result;
    return (verify_control & kControlInterruptEnableMask) == 0U && (verify_extension & kExtensionTimerEnable) == 0U
               ? RtcWakeupResult{}
               : RtcWakeupResult{ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_RESPONSE};
}

}  // namespace m5sw::wakeup
