#include "m5sw_imu_wakeup.hpp"

#include "bmi2_defs.h"
#include "esp_log.h"
#include "m5sw_board.hpp"

namespace m5sw::wakeup {
namespace {

constexpr char kTag[] = "m5sw_imu_wakeup";

ImuWakeupResult configure_reference_sensor_load(bmi270_tools* imu)
{
    if (imu == nullptr) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    const bmi270_tools::sensor_config_t sensor_config = {
        bmi270_tools::ACCEL_RANGE_2G,
        bmi270_tools::GYRO_RANGE_2000,
        bmi270_tools::ODR_100HZ,
        bmi270_tools::ODR_100HZ,
    };
    const bmi270_tools::sensor_performance_config_t performance_config = {
        BMI2_PERF_OPT_MODE,
        BMI2_ACC_OSR2_AVG2,
        BMI2_PERF_OPT_MODE,
        BMI2_POWER_OPT_MODE,
        BMI2_GYR_OSR2_MODE,
        BMI2_GYR_OIS_2000,
    };

    const esp_err_t result =
        imu->configure_sensors(sensor_config, performance_config);
    if (result != ESP_OK) return {result, result};
    ESP_LOGI(kTag, "Reference sensor load verified through BMI270 API");
    return {};
}

ImuWakeupResult configure_low_power_sensor_load(bmi270_tools* imu)
{
    if (imu == nullptr) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    // Any-motion operates on 50 Hz feature samples, so 50 Hz is the lowest
    // accelerometer ODR that preserves the intended wake timing.
    const bmi270_tools::sensor_config_t sensor_config = {
        bmi270_tools::ACCEL_RANGE_2G,
        bmi270_tools::GYRO_RANGE_2000,
        bmi270_tools::ODR_50HZ,
        bmi270_tools::ODR_25HZ,
    };
    const bmi270_tools::sensor_performance_config_t performance_config = {
        BMI2_POWER_OPT_MODE,
        BMI2_ACC_OSR4_AVG1,
        BMI2_POWER_OPT_MODE,
        BMI2_POWER_OPT_MODE,
        BMI2_GYR_OSR4_MODE,
        BMI2_GYR_OIS_2000,
    };

    const esp_err_t result =
        imu->configure_sensors(sensor_config, performance_config);
    if (result != ESP_OK) return {result, result};
    ESP_LOGI(kTag,
             "Low-power sensor load verified through BMI270 API: accel=50Hz "
             "range=2g filter=power_optimized avg=1 gyro=off");
    return {};
}

ImuWakeupResult verify_low_power_sensor_enable_state(bmi270_tools* imu)
{
    if (imu == nullptr || imu->device() == nullptr) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    std::uint8_t power_control = 0;
    const esp_err_t result = i2c_bus_read_byte(
        imu->device(), BMI2_PWR_CTRL_ADDR, &power_control);
    if (result != ESP_OK) return {result, result};

    constexpr std::uint8_t kSensorPowerMask =
        BMI2_AUX_EN_MASK | BMI2_GYR_EN_MASK |
        BMI2_ACC_EN_MASK | BMI2_TEMP_EN_MASK;
    if ((power_control & kSensorPowerMask) != BMI2_ACC_EN_MASK) {
        return {ESP_ERR_INVALID_RESPONSE, power_control};
    }
    ESP_LOGI(kTag,
             "Low-power sensor enable state verified: accel=on gyro=off "
             "aux=off temp=off");
    return {};
}

}  // namespace

ImuWakeupResult ImuWakeup::read_retained_status(i2c_bus_handle_t bus,
                                                ImuStatus* status)
{
    if (bus == nullptr || status == nullptr) {
        return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    }

    i2c_bus_device_handle_t device = i2c_bus_device_create(
        bus, m5sw::board::kImuAddress, 100000);
    if (device == nullptr) return {ESP_ERR_NO_MEM, ESP_ERR_NO_MEM};

    std::uint8_t data[2] = {};
    const esp_err_t result = i2c_bus_read_bytes(
        device, BMI2_INT_STATUS_0_ADDR, sizeof(data), data);
    i2c_bus_device_delete(&device);
    if (result != ESP_OK) return {result, result};

    status->raw = static_cast<std::uint16_t>(data[0]) |
                  (static_cast<std::uint16_t>(data[1]) << 8);
    status->any_motion = (status->raw & BMI270_ANY_MOT_STATUS_MASK) != 0U;
    return {};
}

ImuWakeupResult ImuWakeup::begin(i2c_bus_handle_t bus)
{
    if (bus == nullptr) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    // The Context variant initializes the basic sensors but does not expose the
    // any-motion feature configuration table. Any-motion wake therefore uses the
    // Base variant, whose public wrapper supports feature get/set operations.
    const esp_err_t result = imu_.init(bus, nullptr, false, bmi270_tools::MODE_BASE);
    initialized_ = result == ESP_OK;
    return {result, result};
}

ImuWakeupResult ImuWakeup::configure_any_motion(ImuWakeProfile profile,
                                                std::uint8_t duration,
                                                std::uint8_t threshold)
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    if (profile == ImuWakeProfile::reference) {
        const ImuWakeupResult reference_result =
            configure_reference_sensor_load(&imu_);
        if (!reference_result.ok()) return reference_result;
    } else {
        const ImuWakeupResult low_power_result =
            configure_low_power_sensor_load(&imu_);
        if (!low_power_result.ok()) return low_power_result;
    }

    const bmi270_tools::sensor_type_t low_power_sensors[] = {
        bmi270_tools::SENSOR_ACCEL,
        static_cast<bmi270_tools::sensor_type_t>(BMI2_ANY_MOTION),
    };
    const bmi270_tools::sensor_type_t reference_sensors[] = {
        bmi270_tools::SENSOR_ACCEL,
        bmi270_tools::SENSOR_GYRO,
        static_cast<bmi270_tools::sensor_type_t>(BMI2_WRIST_WEAR_WAKE_UP),
        static_cast<bmi270_tools::sensor_type_t>(BMI2_ANY_MOTION),
    };
    const bmi270_tools::sensor_type_t* sensors =
        profile == ImuWakeProfile::reference ? reference_sensors : low_power_sensors;
    const std::uint8_t sensor_count =
        profile == ImuWakeProfile::reference ? 4U : 2U;
    esp_err_t result = imu_.enable_sensors(sensors, sensor_count);
    if (result != ESP_OK) return {result, result};
    if (profile == ImuWakeProfile::low_power) {
        const ImuWakeupResult enable_state_result =
            verify_low_power_sensor_enable_state(&imu_);
        if (!enable_state_result.ok()) return enable_state_result;
    }
    result = imu_.configure_any_motion({duration, threshold});
    if (result != ESP_OK) return {result, result};
    result = imu_.map_interrupt_to_pin(BMI2_ANY_MOTION, bmi270_tools::INT_PIN_1);
    if (result != ESP_OK) return {result, result};
    // Match the hardware-validated reference path: PM1 GPIO0 wakes on the
    // falling edge produced when the active-high, non-latched INT1 pulse ends.
    result = imu_.enable_interrupt(bmi270_tools::INT_PIN_1, true, false, false);
    if (result != ESP_OK) return {result, result};
    any_motion_configured_ = true;
    profile_ = profile;
    return {};
}

ImuWakeupResult ImuWakeup::read_status(ImuStatus* status) const
{
    if (status == nullptr) return {ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_ARG};
    if (!initialized_ || imu_.device() == nullptr) {
        return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    }
    std::uint8_t data[2] = {};
    const esp_err_t result = i2c_bus_read_bytes(imu_.device(), BMI2_INT_STATUS_0_ADDR,
                                                sizeof(data), data);
    if (result != ESP_OK) return {result, result};
    status->raw = static_cast<std::uint16_t>(data[0]) |
                  (static_cast<std::uint16_t>(data[1]) << 8);
    status->any_motion = (status->raw & BMI270_ANY_MOT_STATUS_MASK) != 0U;
    return {};
}

ImuWakeupResult ImuWakeup::clear_status()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    const esp_err_t result = imu_.clear_interrupt();
    return {result, result};
}

ImuWakeupResult ImuWakeup::disable()
{
    if (!initialized_) return {ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_STATE};
    const bmi270_tools::sensor_type_t low_power_sensors[] = {
        bmi270_tools::SENSOR_ACCEL,
        static_cast<bmi270_tools::sensor_type_t>(BMI2_ANY_MOTION),
    };
    const bmi270_tools::sensor_type_t reference_sensors[] = {
        bmi270_tools::SENSOR_ACCEL,
        bmi270_tools::SENSOR_GYRO,
        static_cast<bmi270_tools::sensor_type_t>(BMI2_WRIST_WEAR_WAKE_UP),
        static_cast<bmi270_tools::sensor_type_t>(BMI2_ANY_MOTION),
    };
    const bool reference_configured =
        any_motion_configured_ && profile_ == ImuWakeProfile::reference;
    const bmi270_tools::sensor_type_t* sensors =
        reference_configured ? reference_sensors : low_power_sensors;
    const std::uint8_t sensor_count =
        reference_configured ? 4U : (any_motion_configured_ ? 2U : 1U);
    const esp_err_t sensor_result = imu_.disable_sensors(sensors, sensor_count);
    if (sensor_result != ESP_OK) return {sensor_result, sensor_result};
    const esp_err_t interrupt_result = imu_.disable_interrupt(bmi270_tools::INT_PIN_1);
    any_motion_configured_ = false;
    profile_ = ImuWakeProfile::low_power;
    return {interrupt_result, interrupt_result};
}

}  // namespace m5sw::wakeup
