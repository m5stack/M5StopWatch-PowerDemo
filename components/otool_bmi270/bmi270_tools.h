/*
 * SPDX-FileCopyrightText: 2025 exia
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __BMI270_TOOLS_H__
#define __BMI270_TOOLS_H__

#include <stdint.h>
#include "math.h"
#include "esp_err.h"
#include "i2c_bus.h"
// #include "i2c_bus_tools.h"

#include "bmi270.h"
#include "bmi2.h"
// Optional maximum-FIFO variant is not included in this snapshot.
#include "bmi270_context.h"         // BMI270 Context API support
#include "bmm150.h"
#include <float.h>

#define I2C_MASTER_TIMEOUT_MS 100

#define BMI2_FIFO_AUX_FRAME_COUNT        UINT8_C(185)
#define BMI2_FIFO_ACCEL_FRAME_COUNT     UINT8_C(185)
#define BMI2_FIFO_GYRO_FRAME_COUNT      UINT8_C(185)

#define BMI270_READ_WRITE_LEN       46

/*! Earth's gravity in m/s^2 */
#define GRAVITY_EARTH  (9.80665f)

#define BMI2_FIFO_RAW_DATA_BUFFER_SIZE 2048
#define BMI2_FIFO_RAW_DATA_USER_LENGTH 2048

#ifndef I2C_BMI270_ADDR
#define I2C_BMI270_ADDR 0x68
#endif

/**
 * @brief I2C wrapper for the BMI270 six-axis sensor (accelerometer and gyroscope).
 * 
 * Provides sensor initialization, configuration, data access, and interrupt control.
 */
class bmi270_tools {
public:
    // BMI270 driver variant selection.
    enum bmi270_mode_t {
        MODE_CONTEXT = 0,           // Lightweight context-oriented feature set.
        MODE_BASE = 1,              // Full traditional feature set.
        MODE_LEGACY = 2,            // Compatibility mode for legacy features.
        MODE_MAXIMUM_FIFO = 3       // Variant with the maximum FIFO buffer.
    };

    // Sensor type enumeration.
    enum sensor_type_t {
        SENSOR_ACCEL = BMI2_ACCEL,
        SENSOR_GYRO = BMI2_GYRO,
        SENSOR_AUX = BMI2_AUX
    };

    // Accelerometer range configuration.
    enum accel_range_t {
        ACCEL_RANGE_2G = BMI2_ACC_RANGE_2G,
        ACCEL_RANGE_4G = BMI2_ACC_RANGE_4G,
        ACCEL_RANGE_8G = BMI2_ACC_RANGE_8G,
        ACCEL_RANGE_16G = BMI2_ACC_RANGE_16G
    };

    // Gyroscope range configuration.
    enum gyro_range_t {
        GYRO_RANGE_125 = BMI2_GYR_RANGE_125,
        GYRO_RANGE_250 = BMI2_GYR_RANGE_250,
        GYRO_RANGE_500 = BMI2_GYR_RANGE_500,
        GYRO_RANGE_1000 = BMI2_GYR_RANGE_1000,
        GYRO_RANGE_2000 = BMI2_GYR_RANGE_2000
    };

    // Output data-rate configuration.
    enum odr_t {
        ODR_25HZ = BMI2_ACC_ODR_25HZ,
        ODR_50HZ = BMI2_ACC_ODR_50HZ,
        ODR_100HZ = BMI2_ACC_ODR_100HZ,
        ODR_200HZ = BMI2_ACC_ODR_200HZ,
        ODR_400HZ = BMI2_ACC_ODR_400HZ,
        ODR_800HZ = BMI2_ACC_ODR_800HZ,
        ODR_1600HZ = BMI2_ACC_ODR_1600HZ
    };

    // Interrupt pin configuration.
    enum int_pin_t {
        INT_PIN_1 = BMI2_INT1,
        INT_PIN_2 = BMI2_INT2,
        INT_PIN_BOTH = BMI2_INT_BOTH
    };

    // Sensor data structure.
    struct sensor_data_t {
        float acc_x, acc_y, acc_z;      // Accelerometer data (LSB).
        float gyr_x, gyr_y, gyr_z;      // Gyroscope data (LSB).
        int16_t mag_x, mag_y, mag_z;    // Magnetometer data (LSB).
        uint32_t sensor_time;           // Sensor timestamp.
        bool acc_valid;                 // Accelerometer data validity flag.
        bool gyr_valid;                 // Gyroscope data validity flag.
        bool mag_valid;                 // Magnetometer data validity flag.
    };

    // Magnetometer data structure.
    struct mag_data_t {
        int32_t x, y, z;                  // Magnetic field data (uT).
        bool valid;                       // Magnetometer data validity flag.
    };

    // Quaternion data structure.
    struct quaternion_data_t {
        float x, y, z, w;                 // Quaternion components.
        bool valid;                       // Quaternion data validity flag.
    };
    
    // Angle unwrapping state.
    struct angle_unwrapper_t {
        float cumulative_offset[3];      // Cumulative offset (turn count * 360 degrees).
        float last_wrapped_angle[3];     // Previous wrapped angle.
        float last_unwrapped_angle[3];   // Previous unwrapped angle.
        bool initialized;                // Initialization flag.
    };

    // Quaternion filter state.
    struct quaternion_filter_t {
        float q0, q1, q2, q3;            // Filtered quaternion.
        float alpha;                     // Filter coefficient.
        bool initialized;                // Initialization flag.
    };

    // Euler angle data structure.
    struct euler_angles_t {
        float roll, pitch, yaw;           // Euler angle (degrees)
        bool valid;                       // Euler angle validity flag.
    };

    // Absolute orientation data.
    struct absolute_orientation_t {
        float x_rotation;                 // Rotation around the X axis (degrees).
        float y_rotation;                 // Rotation around the Y axis (degrees).
        float z_rotation;                 // Rotation around the Z axis (degrees).
        bool valid;                       // Data validity flag.
    };

    // Madgwick filter parameters.
    struct madgwick_filter_t {
        float beta;                       // Filter gain parameter (0.1-2.0).
        float q0, q1, q2, q3;            // quaternion state
        uint32_t last_update_ms;         // Timestamp of the previous update.
        bool initialized;                // Filter initialization flag.
        float adaptive_gain;             // adaptive gain
        float mag_norm_ref;              // Reference magnetic-field magnitude.
        uint32_t stable_count;           // Number of stable updates.
    };

    // Low-pass filter state.
    struct low_pass_filter_t {
        float x, y, z;                   // Filter state.
        float alpha;                     // Filter coefficient.
        bool initialized;                // Initialization flag.
    };

    // sensor data filter group
    struct sensor_filters_t {
        low_pass_filter_t acc_filter;    // Accelerometer low-pass filter.
        low_pass_filter_t gyro_filter;   // Gyroscope low-pass filter.
        low_pass_filter_t mag_filter;    // Magnetometer low-pass filter.
        low_pass_filter_t angle_filter;  // Attitude-angle low-pass filter.
    };

    // Stationary-motion detector state.
    struct motion_detector_t {
        float acc_threshold;             // acceleration-change threshold
        float gyro_threshold;            // gyroscope-change threshold
        uint32_t stable_time_ms;         // stationary-time threshold
        uint32_t last_motion_time;       // last detected-motion time
        bool is_stationary;              // whether currently stationary
        float acc_variance[3];           // Accelerometer variance.
        float gyro_variance[3];          // Gyroscope variance.
        uint32_t sample_count;           // Number of samples.
        float acc_sum[3], acc_sum_sq[3]; // used to calculate variance
        float gyro_sum[3], gyro_sum_sq[3];
    };

    // Gyroscope bias calibration state.
    struct gyro_bias_calibration_t {
        float bias[3];                   // Estimated bias.
        bool calibrated;                 // whether calibrated
        uint32_t calibration_samples;    // Number of calibration samples.
        float calibration_sum[3];        // Accumulated calibration value.
    };

    // Sensor calibration parameters.
    struct sensor_calibration_t {
        // Accelerometer offset and scale.
        float acc_offset[3];
        float acc_scale[3];
        // Gyroscope offset.
        float gyro_offset[3];
        // Magnetometer hard-iron and soft-iron compensation.
        float mag_offset[3];              // hard-iron offset
        float mag_scale[3][3];            // soft-iron correction matrix
        bool calibrated;
    };

    // Sensor configuration structure.
    struct sensor_config_t {
        accel_range_t acc_range;
        gyro_range_t gyr_range;
        odr_t acc_odr;
        odr_t gyr_odr;
    };

    // Accelerometer and gyroscope filter settings not covered by sensor_config_t.
    struct sensor_performance_config_t {
        uint8_t acc_filter_perf;
        uint8_t acc_bwp;
        uint8_t gyr_filter_perf;
        uint8_t gyr_noise_perf;
        uint8_t gyr_bwp;
        uint8_t gyr_ois_range;
    };

    // Motion detection configuration.
    struct motion_config_t {
        uint8_t duration;    // Motion duration (LSB = 20 ms).
        uint8_t threshold;   // Motion threshold (LSB = 0.48 mg).
    };

public:
    /**
     * @brief Creates a BMI270 wrapper.
     * @param addr BMI270 I2C address.
     * @param bus_freq_hz I2C bus frequency (default 100 kHz).
     */
    explicit bmi270_tools(uint8_t addr = I2C_BMI270_ADDR, uint32_t bus_freq_hz = 100000);
    ~bmi270_tools();

    /**
     * @brief Initializes the BMI270 sensor.
     * @param bus I2C master bus handle.
     * @param i2c_device Optional existing device handle; null creates one internally.
     * @param enable_magnetometer Enables BMM150 support when true.
     * @param mode Selects the BMI270 driver variant.
     */
    esp_err_t init(i2c_bus_handle_t bus, i2c_bus_device_handle_t *i2c_device = nullptr, bool enable_magnetometer = false, bmi270_mode_t mode = MODE_CONTEXT);

    /**
     * @brief Deinitializes the sensor and releases any owned device handle.
     */
    esp_err_t deinit();

    // === Sensor configuration ===
    /**
     * @brief Enables the requested sensor features.
     */
    esp_err_t enable_sensors(const sensor_type_t *sensors, uint8_t count);

    /**
     * @brief Disables the requested sensor features.
     */
    esp_err_t disable_sensors(const sensor_type_t *sensors, uint8_t count);

    /**
     * @brief Applies accelerometer, gyroscope and output-rate configuration.
     * @param config Sensor configuration structure.
     */
    esp_err_t configure_sensors(const sensor_config_t &config);

    /**
     * @brief Applies sensor configuration including filter performance settings.
     * @param config Sensor range and output-rate configuration.
     * @param performance Sensor filter and performance configuration.
     */
    esp_err_t configure_sensors(const sensor_config_t &config,
                                const sensor_performance_config_t &performance);

    /**
     * @brief Enables the default accelerometer and gyroscope configuration.
     */
    esp_err_t enable_default_sensors();

    // === Data access ===
    /**
     * @brief Reads converted sensor data.
     * @param data Output data structure.
     */
    esp_err_t get_sensor_data(sensor_data_t &data);

    /**
     * @brief Reads the latest raw BMI270 sensor frame.
     * @param data Output BMI270 API data structure.
     */
    esp_err_t get_raw_sensor_data(struct bmi2_sens_data *data);

    /**
     * @brief Checks whether a BMI270 interrupt is pending.
     */
    bool check_interrupt_status();

    /**
     * @brief Clears the BMI270 interrupt status.
     */
    esp_err_t clear_interrupt();

    /**
     * @brief Enables interrupt output on the selected pin.
     * @param pin Interrupt pin selection.
     * @param active_high True for active-high output.
     * @param open_drain True for open-drain output.
     * @param latch True to latch the interrupt until status is read.
     */
    esp_err_t enable_interrupt(int_pin_t pin = INT_PIN_1, bool active_high = false, 
                              bool open_drain = true, bool latch = true);

    /**
     * @brief Disables interrupt output on the selected pin.
     * @param pin Interrupt pin selection.
     */
    esp_err_t disable_interrupt(int_pin_t pin = INT_PIN_1);

    /**
     * @brief Maps an interrupt source to a BMI270 interrupt pin.
     * @param interrupt_type BMI270 interrupt source identifier.
     * @param pin Interrupt pin selection.
     */
    esp_err_t map_interrupt_to_pin(uint8_t interrupt_type, int_pin_t pin);

    /**
     * @brief Configures the BMI270 any-motion interrupt.
     * @param config Any-motion configuration parameters.
     */
    esp_err_t configure_any_motion(const motion_config_t &config);

    /**
     * @brief Converts accelerometer LSB values to mg.
     * @param lsb Raw accelerometer value.
     * @param range Current accelerometer range.
     */
    static float convert_accel_lsb_to_mg(int16_t lsb, accel_range_t range);

    /**
     * @brief Converts gyroscope LSB values to degrees per second.
     * @param lsb Raw gyroscope value.
     * @param range Current gyroscope range.
     */
    static float convert_gyro_lsb_to_dps(int16_t lsb, gyro_range_t range);

    /**
     * @brief Reads the BMI270 chip ID.
     * @param chip_id Output chip ID.
     */
    esp_err_t get_chip_id(uint8_t &chip_id);

    /**
     * @brief Performs a BMI270 soft reset and invalidates cached state.
     */
    esp_err_t soft_reset();

    int configure_magnetometer();                                  // Configure the BMM150 magnetometer.
    int read_magnetometer_data(mag_data_t* mag_data);              // Read magnetometer data.

    // BMI270 implementation note.
    /**
     * See the BMI270 API documentation.
     * @param quat_data outputquaterniondata
     * See the BMI270 API documentation.
     */
    esp_err_t get_quaternion_data(quaternion_data_t &quat_data);

    /**
     * See the BMI270 API documentation.
     * @param quat_data outputquaterniondata
     * See the BMI270 API documentation.
     * See the BMI270 API documentation.
     */
    esp_err_t get_quaternion_madgwick(quaternion_data_t &quat_data, bool use_magnetometer = true);

    /**
     * @brief calibrationsensor
     * See the BMI270 API documentation.
     * See the BMI270 API documentation.
     * See the BMI270 API documentation.
     */
    esp_err_t calibrate_sensors(uint32_t calibration_time_ms = 10000, uint32_t sample_count = 1000);

    /**
     * @brief setMadgwickfilterparameters
     * See the BMI270 API documentation.
     */
    void set_madgwick_beta(float beta);

    /**
     * See the BMI270 API documentation.
     */
    void reset_attitude_filter();

    /**
     * See the BMI270 API documentation.
     * @param quat inputquaternion
     * @return XYZaxisrotationangledata
     */
    static absolute_orientation_t quaternion_to_xyz_rotation(const quaternion_data_t &quat);

    /**
     * See the BMI270 API documentation.
     * See the BMI270 API documentation.
     */
    esp_err_t get_absolute_orientation(absolute_orientation_t &orientation);
    
    /**
     * See the BMI270 API documentation.
     * See the BMI270 API documentation.
     */
    void reset_angle_tracking();

    // BMI270 implementation note.
    i2c_bus_device_handle_t device() const;
    uint8_t address() const;
    bool is_initialized() const;

private:
    // BMI270 implementation note.
    static int8_t bmi2_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t bmi2_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t bmi2_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t bmi2_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
    static int8_t bmm_i2c_read(uint8_t reg_addr, uint8_t *aux_data, uint32_t len,  void *intf_ptr);
    static int8_t bmm_i2c_write(uint8_t reg_addr, const uint8_t *aux_data, uint32_t len,  void *intf_ptr);
    static void delay_us(uint32_t period, void *intf_ptr);

    // BMI270 implementation note.
    esp_err_t map_feature_interrupt(uint8_t feature_type, int_pin_t pin);
    static float lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width);
    static void print_bmi2_api_error(int8_t rslt);
    static void print_bmm150_api_error(int8_t rslt);
    
    // checksensorenablestatus
    bool is_magnetometer_enabled() const { return (_enabled_sensors_mask & (1ULL << BMI2_AUX)) != 0; }

    // BMI270 implementation note.
    void madgwick_update_6dof(float gx, float gy, float gz, float ax, float ay, float az, float dt);
    void madgwick_update_9dof(float gx, float gy, float gz, float ax, float ay, float az, 
                             float mx, float my, float mz, float dt);
    static float inv_sqrt(float x);  // BMI270 implementation note.
    
    // BMI270 implementation note.
    void apply_low_pass_filter_3d(low_pass_filter_t &filter, float &x, float &y, float &z);
    void apply_low_pass_filter_1d(float &current, float new_value, float alpha);
    bool validate_sensor_data(const sensor_data_t &data);
    float calculate_adaptive_gain(float acc_norm, float gyro_norm, float mag_norm);
    void normalize_angles(absolute_orientation_t &orientation);
    float angle_difference(float angle1, float angle2);
    
    // motion detection and calibration functions
    void update_motion_detector(const sensor_data_t &data);
    bool is_device_stationary();
    void update_gyro_bias_calibration(const sensor_data_t &data);
    void apply_gyro_bias_correction(sensor_data_t &data);
    void reset_orientation_drift();
    void ensure_angle_continuity(absolute_orientation_t &current, const absolute_orientation_t &previous);
    void apply_quaternion_filter(float &q0, float &q1, float &q2, float &q3, bool is_stationary);
    float unwrap_angle(float angle, int axis);  // BMI270 implementation note.
    absolute_orientation_t quaternion_to_xyz_rotation_stable(const quaternion_data_t &quat);
    void reset_angle_unwrapper();  // resetangleunwrapper
    
    // BMI270 implementation note.
    esp_err_t calibrate_gyroscope(uint32_t sample_count);
    esp_err_t calibrate_accelerometer(uint32_t sample_count);
    esp_err_t calibrate_magnetometer(uint32_t sample_count);
    void apply_sensor_calibration(sensor_data_t &data);

    // BMI270 implementation note.
    esp_err_t restore_state_after_reset();
    esp_err_t configure_sensors_impl(
        const sensor_config_t &config,
        const sensor_performance_config_t *performance);

private:
    i2c_bus_handle_t _bus;  // BMI270 implementation note.
    i2c_bus_device_handle_t _dev;
    uint8_t _addr;
    uint32_t _bus_freq;
    bool _initialized;
    bool _owns_dev;

    struct bmi2_fifo_frame fifoframe = { 0 };
    uint8_t fifo_data[BMI2_FIFO_RAW_DATA_BUFFER_SIZE];
    
    struct bmi2_dev _bmi270_dev;        // BMI270 APIdevicestructure
    struct bmm150_dev _bmm150_dev;      // BMM150 APIdevicestructure
    struct bmm150_settings _bmm150_mag_settings;    // BMM150 configurationstructure
    struct bmm150_mag_data _bmm150_mag_data;    // BMM150 magnetometerdatastructure
    sensor_config_t _current_config; // currentconfiguration
    bmi270_mode_t _current_mode;     // BMI270 implementation note.
    // BMI270 implementation note.
    uint64_t _enabled_sensors_mask = 0;

    // BMI270 implementation note.
    madgwick_filter_t _madgwick_filter = {0};     // Madgwickfilterstatus
    sensor_calibration_t _calibration = {0};     // sensorcalibrationparameters
    sensor_filters_t _filters = {0};             // sensor data filter group
    absolute_orientation_t _last_orientation = {0}; // BMI270 implementation note.
    motion_detector_t _motion_detector = {0};     // BMI270 implementation note.
    gyro_bias_calibration_t _gyro_bias = {0};     // gyroscopebiascalibration
    quaternion_filter_t _quat_filter = {0};       // quaternionfilter
    angle_unwrapper_t _angle_unwrapper = {0};     // BMI270 implementation note.

    // BMI270 implementation note.
    struct {
        bool initialized = false;
        float last_roll = 0.0f;
        float last_pitch = 0.0f;
        float last_yaw = 0.0f;
        float roll_rate = 0.0f;   // BMI270 implementation note.
        float pitch_rate = 0.0f;
        float yaw_rate = 0.0f;
    } _angle_tracker;
    
    // BMI270 implementation note.
    float smart_angle_unwrap(float current_angle, float previous_angle);
    float smooth_angle_transition(float new_angle, float old_angle, float factor);

};

#endif // __BMI270_TOOLS_H__
