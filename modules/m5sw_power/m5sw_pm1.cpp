#include "m5sw_pm1.hpp"

namespace m5sw::power {
namespace {

esp_err_t map_error(m5pm1_err_t error)
{
    switch (error) {
        case M5PM1_OK:
            return ESP_OK;
        case M5PM1_ERR_INVALID_ARG:
            return ESP_ERR_INVALID_ARG;
        case M5PM1_ERR_NOT_INIT:
            return ESP_ERR_INVALID_STATE;
        case M5PM1_ERR_NOT_SUPPORTED:
            return ESP_ERR_NOT_SUPPORTED;
        case M5PM1_ERR_TIMEOUT:
            return ESP_ERR_TIMEOUT;
        default:
            return ESP_FAIL;
    }
}

m5pm1_irq_gpio_t irq_for_gpio(Pm1Gpio gpio)
{
    switch (gpio) {
        case Pm1Gpio::rtc_imu:
            return M5PM1_IRQ_GPIO0;
        case Pm1Gpio::port:
            return M5PM1_IRQ_GPIO4;
    }
    return M5PM1_IRQ_GPIO_NONE;
}

}  // namespace

DriverResult Pm1Adapter::result(m5pm1_err_t raw) const
{
    return DriverResult{map_error(raw), static_cast<int>(raw)};
}

DriverResult Pm1Adapter::begin(i2c_bus_handle_t bus, std::uint32_t speed_hz)
{
    if (bus == nullptr || (speed_hz != 100000U && speed_hz != 400000U)) {
        return DriverResult{ESP_ERR_INVALID_ARG, M5PM1_ERR_INVALID_ARG};
    }
    const m5pm1_err_t raw     = pm1_.begin(bus, m5sw::board::kPm1Address, speed_hz);
    const DriverResult mapped = result(raw);
    initialized_              = mapped.ok();
    return mapped;
}

DriverResult Pm1Adapter::set_power_config(std::uint8_t mask, std::uint8_t value)
{
    if (!initialized_) {
        return result(M5PM1_ERR_NOT_INIT);
    }
    const DriverResult written = result(pm1_.setPowerConfig(mask, value));
    if (!written.ok()) {
        return written;
    }
    return verify_mask(mask, value);
}

DriverResult Pm1Adapter::set_external_5v_output(bool enabled)
{
    return set_power_config(M5PM1_PWR_CFG_BOOST_EN, enabled ? M5PM1_PWR_CFG_BOOST_EN : 0U);
}

DriverResult Pm1Adapter::set_status_led(bool enabled)
{
    return set_power_config(M5PM1_PWR_CFG_LED_CTRL, enabled ? M5PM1_PWR_CFG_LED_CTRL : 0U);
}

DriverResult Pm1Adapter::set_l1_rail(bool enabled)
{
    return set_power_config(M5PM1_PWR_CFG_LDO_EN, enabled ? M5PM1_PWR_CFG_LDO_EN : 0U);
}

DriverResult Pm1Adapter::disable_l1_rail_final()
{
    if (!initialized_) {
        return result(M5PM1_ERR_NOT_INIT);
    }

    const bool auto_snapshot = pm1_.isAutoSnapshotEnabled();
    pm1_.setAutoSnapshot(false);
    const DriverResult written = result(pm1_.setPowerConfig(M5PM1_PWR_CFG_LDO_EN, 0U));
    if (!written.ok()) {
        pm1_.setAutoSnapshot(auto_snapshot);
        return written;
    }

    // The shared I2C pull-up domain is now unavailable. Invalidate this adapter
    // so callers cannot accidentally issue another PM1 transaction.
    initialized_ = false;
    return {};
}

DriverResult Pm1Adapter::read_power_config(std::uint8_t* config)
{
    if (!initialized_ || config == nullptr) {
        return result(config == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.getPowerConfig(config));
}

DriverResult Pm1Adapter::verify_mask(std::uint8_t mask, std::uint8_t expected)
{
    std::uint8_t actual     = 0;
    const DriverResult read = result(pm1_.getPowerConfig(&actual));
    if (!read.ok()) {
        return read;
    }
    if ((actual & mask) != (expected & mask)) {
        return DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
    }
    return {};
}

DriverResult Pm1Adapter::set_ldo_hold(bool enabled)
{
    if (!initialized_) {
        return result(M5PM1_ERR_NOT_INIT);
    }
    const DriverResult written = result(pm1_.ldoSetPowerHold(enabled));
    if (!written.ok()) {
        return written;
    }
    bool actual             = false;
    const DriverResult read = result(pm1_.ldoGetPowerHold(&actual));
    if (!read.ok()) {
        return read;
    }
    return actual == enabled ? DriverResult{} : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::read_ldo_hold(bool* enabled)
{
    if (!initialized_ || enabled == nullptr) {
        return result(enabled == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.ldoGetPowerHold(enabled));
}

DriverResult Pm1Adapter::set_watchdog_timeout(std::uint8_t timeout_sec)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    DriverResult written = result(pm1_.wdtSet(timeout_sec));
    if (!written.ok()) return written;
    std::uint8_t actual = 0;
    written             = result(pm1_.wdtGetCount(&actual));
    if (!written.ok()) return written;
    return timeout_sec == 0U && actual == 0U ? DriverResult{}
                                             : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::configure_all_gpio_safe()
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    m5pm1_err_t raw = pm1_.irqSetGpioMaskAll(M5PM1_IRQ_MASK_ENABLE);
    if (raw != M5PM1_OK) return result(raw);
    for (std::uint8_t index = 0; index < M5PM1_MAX_GPIO_PINS; ++index) {
        const auto pin = static_cast<m5pm1_gpio_num_t>(index);
        raw            = pm1_.gpioSetWakeEnable(pin, false);
        if (raw != M5PM1_OK) return result(raw);
        raw = pm1_.gpioSetFunc(pin, M5PM1_GPIO_FUNC_GPIO);
        if (raw != M5PM1_OK) return result(raw);
        raw = pm1_.gpioSetMode(pin, M5PM1_GPIO_MODE_INPUT);
        if (raw != M5PM1_OK) return result(raw);
        raw = pm1_.gpioSetPull(pin, M5PM1_GPIO_PULL_NONE);
        if (raw != M5PM1_OK) return result(raw);
        raw = pm1_.gpioSetDrive(pin, M5PM1_GPIO_DRIVE_OPENDRAIN);
        if (raw != M5PM1_OK) return result(raw);
    }
    return {};
}

DriverResult Pm1Adapter::mask_non_gpio_irqs()
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    m5pm1_err_t raw = pm1_.irqSetBtnMaskAll(M5PM1_IRQ_MASK_ENABLE);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.irqSetSysMaskAll(M5PM1_IRQ_MASK_ENABLE);
    if (raw != M5PM1_OK) return result(raw);

    std::uint8_t button_mask = 0;
    raw                      = pm1_.irqGetBtnMaskBits(&button_mask);
    if (raw != M5PM1_OK) return result(raw);
    std::uint8_t system_mask = 0;
    raw                      = pm1_.irqGetSysMaskBits(&system_mask);
    if (raw != M5PM1_OK) return result(raw);
    if ((button_mask & M5PM1_IRQ_BTN_ALL) != M5PM1_IRQ_BTN_ALL ||
        (system_mask & M5PM1_IRQ_SYS_ALL) != M5PM1_IRQ_SYS_ALL) {
        return DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
    }
    return {};
}

DriverResult Pm1Adapter::configure_irq_output()
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    constexpr m5pm1_gpio_num_t kIrqOutput = M5PM1_GPIO_NUM_1;

    m5pm1_err_t raw = pm1_.gpioSetWakeEnable(kIrqOutput, false);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetMode(kIrqOutput, M5PM1_GPIO_MODE_INPUT);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetPull(kIrqOutput, M5PM1_GPIO_PULL_UP);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetDrive(kIrqOutput, M5PM1_GPIO_DRIVE_PUSHPULL);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetFunc(kIrqOutput, M5PM1_GPIO_FUNC_IRQ);
    if (raw != M5PM1_OK) return result(raw);
    return result(pm1_.verifyPinConfig(false));
}

DriverResult Pm1Adapter::configure_gpio_wake(Pm1Gpio gpio)
{
    if (!initialized_) {
        return result(M5PM1_ERR_NOT_INIT);
    }
    const auto pin             = static_cast<m5pm1_gpio_num_t>(gpio);
    const m5pm1_irq_gpio_t irq = irq_for_gpio(gpio);
    if (irq == M5PM1_IRQ_GPIO_NONE) return result(M5PM1_ERR_INVALID_ARG);

    m5pm1_err_t raw = pm1_.irqSetGpioMask(irq, M5PM1_IRQ_MASK_ENABLE);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetWakeEnable(pin, false);
    if (raw != M5PM1_OK) return result(raw);
    // PM1 datasheet defines GPIO_FUNC=0b10 as reserved. Wake is controlled by
    // GPIO_WAKE_EN/CFG while the pin remains a normal GPIO input (FUNC=0b00).
    raw = pm1_.gpioSetFunc(pin, M5PM1_GPIO_FUNC_GPIO);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetMode(pin, M5PM1_GPIO_MODE_INPUT);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetPull(pin, M5PM1_GPIO_PULL_UP);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetWakeEdge(pin, M5PM1_GPIO_WAKE_FALLING);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetWakeEnable(pin, true);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.irqSetGpioMask(irq, M5PM1_IRQ_MASK_DISABLE);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.verifyPinConfig(false);
    if (raw != M5PM1_OK) return result(raw);
    m5pm1_irq_mask_ctrl_t mask = M5PM1_IRQ_MASK_ENABLE;
    raw                        = pm1_.irqGetGpioMask(irq, &mask);
    if (raw != M5PM1_OK) return result(raw);
    return mask == M5PM1_IRQ_MASK_DISABLE ? DriverResult{}
                                          : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::disable_gpio_wake(Pm1Gpio gpio)
{
    if (!initialized_) {
        return result(M5PM1_ERR_NOT_INIT);
    }
    const m5pm1_irq_gpio_t irq = irq_for_gpio(gpio);
    if (irq == M5PM1_IRQ_GPIO_NONE) return result(M5PM1_ERR_INVALID_ARG);
    m5pm1_err_t raw = pm1_.gpioSetWakeEnable(static_cast<m5pm1_gpio_num_t>(gpio), false);
    if (raw != M5PM1_OK) return result(raw);
    return result(pm1_.irqSetGpioMask(irq, M5PM1_IRQ_MASK_ENABLE));
}

DriverResult Pm1Adapter::restore_gpio_input(Pm1Gpio gpio)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    const auto pin             = static_cast<m5pm1_gpio_num_t>(gpio);
    const m5pm1_irq_gpio_t irq = irq_for_gpio(gpio);
    if (irq == M5PM1_IRQ_GPIO_NONE) return result(M5PM1_ERR_INVALID_ARG);
    m5pm1_err_t raw = pm1_.gpioSetWakeEnable(pin, false);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.irqSetGpioMask(irq, M5PM1_IRQ_MASK_ENABLE);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetFunc(pin, M5PM1_GPIO_FUNC_GPIO);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetMode(pin, M5PM1_GPIO_MODE_INPUT);
    if (raw != M5PM1_OK) return result(raw);
    raw = pm1_.gpioSetPull(pin, M5PM1_GPIO_PULL_NONE);
    if (raw != M5PM1_OK) return result(raw);
    return result(pm1_.verifyPinConfig(false));
}

DriverResult Pm1Adapter::read_wake_source(std::uint8_t* source)
{
    if (!initialized_ || source == nullptr) {
        return result(source == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.getWakeSource(source, M5PM1_CLEAN_NONE));
}

DriverResult Pm1Adapter::clear_wake_source(std::uint8_t captured_mask)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    if (captured_mask == 0) return {};
    const DriverResult cleared = result(pm1_.clearWakeSource(captured_mask));
    if (!cleared.ok()) return cleared;
    std::uint8_t remaining  = 0;
    const DriverResult read = result(pm1_.getWakeSource(&remaining, M5PM1_CLEAN_NONE));
    if (!read.ok()) return read;
    return (remaining & captured_mask) == 0 ? DriverResult{}
                                            : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::read_gpio_irq(std::uint8_t* status)
{
    if (!initialized_ || status == nullptr) {
        return result(status == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.irqGetGpioStatus(status, M5PM1_CLEAN_NONE));
}

DriverResult Pm1Adapter::clear_gpio_irq(std::uint8_t captured_mask)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    std::uint8_t pending = 0;
    DriverResult read    = read_gpio_irq(&pending);
    if (!read.ok()) return read;
    pending &= captured_mask;
    // CLEAN_ONCE reports one event at a time, so retain the remaining mask and
    // reject a response that does not make progress toward the captured state.
    while (pending != 0) {
        m5pm1_irq_gpio_t event     = M5PM1_IRQ_GPIO_NONE;
        const DriverResult cleared = result(pm1_.irqGetGpioStatusEnum(&event, M5PM1_CLEAN_ONCE));
        if (!cleared.ok()) return cleared;
        const std::uint8_t event_mask = static_cast<std::uint8_t>(event);
        if ((event_mask & pending) == 0) {
            return DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
        }
        pending = static_cast<std::uint8_t>(pending & ~event_mask);
    }
    read = read_gpio_irq(&pending);
    if (!read.ok()) return read;
    return (pending & captured_mask) == 0 ? DriverResult{}
                                          : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::read_button_irq(std::uint8_t* status)
{
    if (!initialized_ || status == nullptr) {
        return result(status == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.irqGetBtnStatus(status, M5PM1_CLEAN_NONE));
}

DriverResult Pm1Adapter::clear_button_irq(std::uint8_t captured_mask)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    std::uint8_t pending = 0;
    DriverResult read    = read_button_irq(&pending);
    if (!read.ok()) return read;
    pending &= captured_mask;
    while (pending != 0) {
        m5pm1_irq_btn_t event      = M5PM1_IRQ_BTN_NONE;
        const DriverResult cleared = result(pm1_.irqGetBtnStatusEnum(&event, M5PM1_CLEAN_ONCE));
        if (!cleared.ok()) return cleared;
        const std::uint8_t event_mask = static_cast<std::uint8_t>(event);
        if ((event_mask & pending) == 0) {
            return DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
        }
        pending = static_cast<std::uint8_t>(pending & ~event_mask);
    }
    read = read_button_irq(&pending);
    if (!read.ok()) return read;
    return (pending & captured_mask) == 0 ? DriverResult{}
                                          : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::read_system_irq(std::uint8_t* status)
{
    if (!initialized_ || status == nullptr) {
        return result(status == nullptr ? M5PM1_ERR_INVALID_ARG : M5PM1_ERR_NOT_INIT);
    }
    return result(pm1_.irqGetSysStatus(status, M5PM1_CLEAN_NONE));
}

DriverResult Pm1Adapter::clear_system_irq(std::uint8_t captured_mask)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    std::uint8_t pending = 0;
    DriverResult read    = read_system_irq(&pending);
    if (!read.ok()) return read;
    pending &= captured_mask;
    while (pending != 0) {
        m5pm1_irq_sys_t event      = M5PM1_IRQ_SYS_NONE;
        const DriverResult cleared = result(pm1_.irqGetSysStatusEnum(&event, M5PM1_CLEAN_ONCE));
        if (!cleared.ok()) return cleared;
        const std::uint8_t event_mask = static_cast<std::uint8_t>(event);
        if ((event_mask & pending) == 0) {
            return DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
        }
        pending = static_cast<std::uint8_t>(pending & ~event_mask);
    }
    read = read_system_irq(&pending);
    if (!read.ok()) return read;
    return (pending & captured_mask) == 0 ? DriverResult{}
                                          : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::set_i2c_sleep_time(std::uint8_t seconds)
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    const DriverResult written = result(pm1_.setI2cSleepTime(seconds));
    if (!written.ok()) return written;
    std::uint8_t actual     = 0;
    const DriverResult read = result(pm1_.getI2cSleepTime(&actual));
    if (!read.ok()) return read;
    return actual == seconds ? DriverResult{} : DriverResult{ESP_ERR_INVALID_RESPONSE, M5PM1_ERR_VERIFY_FAILED};
}

DriverResult Pm1Adapter::shutdown()
{
    if (!initialized_) return result(M5PM1_ERR_NOT_INIT);
    return result(pm1_.shutdown());
}

}  // namespace m5sw::power
