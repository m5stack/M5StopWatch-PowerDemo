/*
 * SPDX-FileCopyrightText: 2025 exia
 *
 * SPDX-License-Identifier: MIT
 */

#include "bmi270_tools.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "bmi270_tools";

bmi270_tools::bmi270_tools(uint8_t addr, uint32_t bus_freq_hz)
    : _bus(nullptr), _dev(nullptr), _addr(addr), _bus_freq(bus_freq_hz), 
      _initialized(false), _owns_dev(false)
{
    // initializationdefault configuration
    _current_config.acc_range = ACCEL_RANGE_4G;
    _current_config.gyr_range = GYRO_RANGE_1000;
    _current_config.acc_odr = ODR_200HZ;
    _current_config.gyr_odr = ODR_200HZ;
    
    // initializationdefault mode
    _current_mode = MODE_CONTEXT;
    
    // BMI270 implementation note.
    memset(&_bmi270_dev, 0, sizeof(_bmi270_dev));
}

bmi270_tools::~bmi270_tools()
{
    deinit();
}

esp_err_t bmi270_tools::init(i2c_bus_handle_t bus, i2c_bus_device_handle_t *i2c_device, bool enable_magnetometer, bmi270_mode_t mode)
{
    if (_initialized) {
        ESP_LOGW(TAG, "BMI270 already initialized @0x%02X", _addr);
        return ESP_OK;
    }
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    _bus = bus;
    _current_mode = mode;  // BMI270 implementation note.
    
    // BMI270 implementation note.
    if (i2c_device == nullptr || *i2c_device == nullptr) {
        // automaticcreatedevicehandle
        _dev = i2c_bus_device_create(_bus, _addr, _bus_freq);
        if (_dev == nullptr) {
            ESP_LOGE(TAG, "Failed to create I2C device for BMI270 @0x%02X", _addr);
            return ESP_FAIL;
        }
        _owns_dev = true;
        ESP_LOGD(TAG, "Auto-created I2C device handle");
    } else {
        // BMI270 implementation note.
        _dev = *i2c_device;
        _owns_dev = false;
        ESP_LOGD(TAG, "Using external I2C device handle");
    }

    // BMI270 implementation note.
    _initialized = true;

    // configuration BMI270 API interface
    _bmi270_dev.intf = BMI2_I2C_INTF;
    _bmi270_dev.read = bmi2_i2c_read;
    _bmi270_dev.write = bmi2_i2c_write;
    _bmi270_dev.delay_us = delay_us;
    _bmi270_dev.read_write_len = BMI270_READ_WRITE_LEN;
    _bmi270_dev.config_file_ptr = NULL;
    _bmi270_dev.intf_ptr = this; // BMI270 implementation note.

    // BMI270 implementation note.
    uint8_t raw_chip_id = 0x00;
    esp_err_t id_ret = i2c_bus_read_byte(_dev, BMI2_CHIP_ID_ADDR, &raw_chip_id);
    if (id_ret != ESP_OK) {
        ESP_LOGE(TAG, "Pre-read CHIP_ID failed (i2c err=%s)", esp_err_to_name(id_ret));
        if (_owns_dev) deinit();
        return id_ret;
    }
    ESP_LOGI(TAG, "BMI270 pre-read CHIP_ID=0x%02X (expected 0x%02X)", raw_chip_id, BMI270_CHIP_ID);
    if (raw_chip_id != BMI270_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected CHIP_ID 0x%02X, abort init", raw_chip_id);
        if (_owns_dev) deinit();
        return ESP_ERR_NOT_FOUND;
    }
    _bmi270_dev.chip_id = raw_chip_id;

    // BMI270 implementation note.
    int8_t rslt = BMI2_OK;
    const char* mode_name = "";
    
    switch (_current_mode) {
        case MODE_CONTEXT:
            ESP_LOGI(TAG, "Starting BMI270 Context initialization...");
            rslt = bmi270_context_init(&_bmi270_dev);
            mode_name = "Context";
            break;
            
        case MODE_BASE:
            ESP_LOGI(TAG, "Starting BMI270 Base initialization...");
            rslt = bmi270_init(&_bmi270_dev);
            mode_name = "Base";
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported BMI270 mode: %d", _current_mode);
            if (_owns_dev) deinit();
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 %s initialization failed", mode_name);
        print_bmi2_api_error(rslt);
        _initialized = false; // resetinitializationflag
        if (_owns_dev) deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BMI270 %s initialized successfully (addr=0x%02X, freq=%lu)", 
             mode_name, _addr, (unsigned long)_bus_freq);
    
    // BMI270 implementation note.
    if (enable_magnetometer) {

        // BMI270 implementation note.
        uint8_t pupsel = BMI2_ASDA_PUPSEL_2K; // 2k internal pull-up
        int8_t trim_rslt = bmi2_set_regs(BMI2_AUX_IF_TRIM, &pupsel, 1, &_bmi270_dev);
        if (trim_rslt != BMI2_OK) {
            ESP_LOGW(TAG, "Set internal AUX pull-up failed rslt=%d", trim_rslt);
        } else {
            ESP_LOGI(TAG, "Internal AUX pull-up enabled (2k)");
        }

        ESP_LOGI(TAG, "Auto configuring BMM150 magnetometer...");
        int mag_ret = configure_magnetometer();
        if (mag_ret == 0) {
            ESP_LOGI(TAG, "BMM150 magnetometer configured successfully");
        } else {
            ESP_LOGW(TAG, "BMM150 magnetometer config failed: %d (continue BMI270 init)", mag_ret);
        }
    }
    
    return ESP_OK;
}

esp_err_t bmi270_tools::deinit()
{
    if (_dev && _owns_dev) {
        i2c_bus_device_delete(&_dev);
    }
    _dev = nullptr;
    _bus = nullptr;
    _initialized = false;
    _owns_dev = false;
    memset(&_bmi270_dev, 0, sizeof(_bmi270_dev));
    return ESP_OK;
}

esp_err_t bmi270_tools::enable_sensors(const sensor_type_t *sensors, uint8_t count)
{
    if (!_initialized || sensors == nullptr || count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // BMI270 implementation note.
    if (count > 8) return ESP_ERR_INVALID_ARG;
    uint8_t sens_list[8];
    for (uint8_t i = 0; i < count; i++) {
        sens_list[i] = (uint8_t)sensors[i];
        _enabled_sensors_mask |= (uint64_t)1 << sens_list[i];
    }

    // BMI270 implementation note.
    int8_t rslt = BMI2_OK;
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_sensor_enable(sens_list, count, &_bmi270_dev);
            break;
            
        case MODE_BASE:
            rslt = bmi270_sensor_enable(sens_list, count, &_bmi270_dev);
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for sensor enable: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable sensors");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Enabled %d sensors", count);
    return ESP_OK;
}

esp_err_t bmi270_tools::disable_sensors(const sensor_type_t *sensors, uint8_t count)
{
    if (!_initialized || sensors == nullptr || count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (count > 8) return ESP_ERR_INVALID_ARG;
    uint8_t sens_list[8];
    for (uint8_t i = 0; i < count; i++) {
        sens_list[i] = (uint8_t)sensors[i];
        _enabled_sensors_mask &= ~((uint64_t)1 << sens_list[i]);
    }

    // BMI270 implementation note.
    int8_t rslt = BMI2_OK;
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_sensor_disable(sens_list, count, &_bmi270_dev);
            break;
            
        case MODE_BASE:
            rslt = bmi270_sensor_disable(sens_list, count, &_bmi270_dev);
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for sensor disable: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }

    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to disable sensors");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Disabled %d sensors", count);
    return ESP_OK;
}

esp_err_t bmi270_tools::configure_sensors(const sensor_config_t &config)
{
    return configure_sensors_impl(config, nullptr);
}

esp_err_t bmi270_tools::configure_sensors(
    const sensor_config_t &config,
    const sensor_performance_config_t &performance)
{
    return configure_sensors_impl(config, &performance);
}

esp_err_t bmi270_tools::configure_sensors_impl(
    const sensor_config_t &config,
    const sensor_performance_config_t *performance)
{
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    struct bmi2_sens_config sens_config[2] = {};
    sens_config[0].type = BMI2_ACCEL;
    sens_config[1].type = BMI2_GYRO;

    // getdefault configuration
    int8_t rslt = bmi2_get_sensor_config(sens_config, 2, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get sensor config");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    // configurationaccelerometer
    sens_config[0].cfg.acc.odr = (uint8_t)config.acc_odr;
    sens_config[0].cfg.acc.range = (uint8_t)config.acc_range;

    // configurationgyroscope
    sens_config[1].cfg.gyr.odr = (uint8_t)config.gyr_odr;
    sens_config[1].cfg.gyr.range = (uint8_t)config.gyr_range;

    if (performance != nullptr) {
        sens_config[0].cfg.acc.filter_perf = performance->acc_filter_perf;
        sens_config[0].cfg.acc.bwp = performance->acc_bwp;
        sens_config[1].cfg.gyr.filter_perf = performance->gyr_filter_perf;
        sens_config[1].cfg.gyr.noise_perf = performance->gyr_noise_perf;
        sens_config[1].cfg.gyr.bwp = performance->gyr_bwp;
        sens_config[1].cfg.gyr.ois_range = performance->gyr_ois_range;
    }

    // BMI270 implementation note.
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_set_sensor_config(sens_config, 2, &_bmi270_dev);
            break;
            
        case MODE_BASE:
            rslt = bmi270_set_sensor_config(sens_config, 2, &_bmi270_dev);
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for sensor config: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to set sensor config");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    if (performance != nullptr) {
        struct bmi2_sens_config actual[2] = {};
        actual[0].type = BMI2_ACCEL;
        actual[1].type = BMI2_GYRO;
        rslt = bmi2_get_sensor_config(actual, 2, &_bmi270_dev);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "Failed to read back sensor config");
            print_bmi2_api_error(rslt);
            return ESP_FAIL;
        }

        const bool acc_matches =
            actual[0].cfg.acc.odr == (uint8_t)config.acc_odr &&
            actual[0].cfg.acc.range == (uint8_t)config.acc_range &&
            actual[0].cfg.acc.filter_perf == performance->acc_filter_perf &&
            actual[0].cfg.acc.bwp == performance->acc_bwp;
        const bool gyr_matches =
            actual[1].cfg.gyr.odr == (uint8_t)config.gyr_odr &&
            actual[1].cfg.gyr.range == (uint8_t)config.gyr_range &&
            actual[1].cfg.gyr.filter_perf == performance->gyr_filter_perf &&
            actual[1].cfg.gyr.noise_perf == performance->gyr_noise_perf &&
            actual[1].cfg.gyr.bwp == performance->gyr_bwp &&
            actual[1].cfg.gyr.ois_range == performance->gyr_ois_range;
        if (!acc_matches || !gyr_matches) {
            ESP_LOGE(TAG,
                     "Accelerometer config mismatch: expected odr=%u range=%u "
                     "filter_perf=%u bwp=%u; actual odr=%u range=%u "
                     "filter_perf=%u bwp=%u",
                     (unsigned)config.acc_odr, (unsigned)config.acc_range,
                     (unsigned)performance->acc_filter_perf,
                     (unsigned)performance->acc_bwp,
                     (unsigned)actual[0].cfg.acc.odr,
                     (unsigned)actual[0].cfg.acc.range,
                     (unsigned)actual[0].cfg.acc.filter_perf,
                     (unsigned)actual[0].cfg.acc.bwp);
            ESP_LOGE(TAG,
                     "Gyroscope config mismatch: expected odr=%u range=%u "
                     "filter_perf=%u noise_perf=%u bwp=%u ois_range=%u; "
                     "actual odr=%u range=%u filter_perf=%u noise_perf=%u "
                     "bwp=%u ois_range=%u",
                     (unsigned)config.gyr_odr, (unsigned)config.gyr_range,
                     (unsigned)performance->gyr_filter_perf,
                     (unsigned)performance->gyr_noise_perf,
                     (unsigned)performance->gyr_bwp,
                     (unsigned)performance->gyr_ois_range,
                     (unsigned)actual[1].cfg.gyr.odr,
                     (unsigned)actual[1].cfg.gyr.range,
                     (unsigned)actual[1].cfg.gyr.filter_perf,
                     (unsigned)actual[1].cfg.gyr.noise_perf,
                     (unsigned)actual[1].cfg.gyr.bwp,
                     (unsigned)actual[1].cfg.gyr.ois_range);
            return ESP_ERR_INVALID_RESPONSE;
        }

        ESP_LOGI(TAG, "Sensor performance configuration verified");
    }

    _current_config = config;
    ESP_LOGI(TAG, "Sensor configuration updated");
    return ESP_OK;
}

esp_err_t bmi270_tools::enable_default_sensors()
{
    // configurationsensor
    esp_err_t ret = configure_sensors(_current_config);
    if (ret != ESP_OK) {
        return ret;
    }

    // BMI270 implementation note.
    sensor_type_t sensors[] = {SENSOR_ACCEL, SENSOR_GYRO, SENSOR_AUX};
    esp_err_t enable_ret = enable_sensors(sensors, 3);
    
    ESP_LOGI(TAG, "Default sensors enabled (ACC, GYR, AUX): %s", 
             enable_ret == ESP_OK ? "SUCCESS" : "FAILED");
    
    return enable_ret;
}

esp_err_t bmi270_tools::get_sensor_data(sensor_data_t &data)
{
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    struct bmi2_sens_data sensor_data;
    int8_t rslt = bmi2_get_sensor_data(&sensor_data, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get sensor data");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    data.sensor_time = (uint32_t)(esp_timer_get_time() / 1000); // useESP32boot time(ms)

    // BMI270 implementation note.
    // BMI270 implementation note.
    if (sensor_data.status & BMI2_DRDY_ACC) {
        accel_range_t ar = _current_config.acc_range;
        float mg_x = convert_accel_lsb_to_mg(sensor_data.acc.x, ar);
        float mg_y = convert_accel_lsb_to_mg(sensor_data.acc.y, ar);
        float mg_z = convert_accel_lsb_to_mg(sensor_data.acc.z, ar);
        data.acc_x = mg_x * 0.001f * GRAVITY_EARTH; // BMI270 implementation note.
        data.acc_y = mg_y * 0.001f * GRAVITY_EARTH;
        data.acc_z = mg_z * 0.001f * GRAVITY_EARTH;
        data.acc_valid = true;
    }
    if (sensor_data.status & BMI2_DRDY_GYR) {
        gyro_range_t gr = _current_config.gyr_range;
        data.gyr_x = convert_gyro_lsb_to_dps(sensor_data.gyr.x, gr);
        data.gyr_y = convert_gyro_lsb_to_dps(sensor_data.gyr.y, gr);
        data.gyr_z = convert_gyro_lsb_to_dps(sensor_data.gyr.z, gr);
        data.gyr_valid = true;
    }
#if 0
    if (data.acc_valid) {
        ESP_LOGD(TAG, "ACC(m/s2)=%.3f %.3f %.3f", data.acc_x, data.acc_y, data.acc_z);
    }
    if (data.gyr_valid) {
        ESP_LOGD(TAG, "GYR(dps)=%.3f %.3f %.3f", data.gyr_x, data.gyr_y, data.gyr_z);
    }
#endif

    if(sensor_data.status & BMI2_DRDY_AUX)
    {    
        struct bmm150_mag_data mag_data;
        rslt = bmm150_aux_mag_data(sensor_data.aux_data, &mag_data, &_bmm150_dev);
        if (rslt == BMM150_OK) {
            data.mag_x = (int16_t)mag_data.x;
            data.mag_y = (int16_t)mag_data.y;
            data.mag_z = (int16_t)mag_data.z;
            data.mag_valid = true;
        }

    }


    return ESP_OK;
}

esp_err_t bmi270_tools::get_raw_sensor_data(struct bmi2_sens_data *data)
{
    if (!_initialized || data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    int8_t rslt = bmi2_get_sensor_data(data, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool bmi270_tools::check_interrupt_status()
{
    if (!_initialized) {
        return false;
    }

    uint16_t int_status = 0;
    int8_t rslt = bmi2_get_int_status(&int_status, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        return false;
    }

    bool interrupt_detected = false;

    // BMI270 implementation note.
    if (int_status & BMI270_SIG_MOT_STATUS_MASK) {
        ESP_LOGI(TAG, "Significant motion detected");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_STEP_CNT_STATUS_MASK) {
        ESP_LOGI(TAG, "Step counter interrupt");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_STEP_ACT_STATUS_MASK) {
        ESP_LOGI(TAG, "Activity recognition interrupt");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_WRIST_WAKE_UP_STATUS_MASK) {
        ESP_LOGI(TAG, "Wrist wake-up detected");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_WRIST_GEST_STATUS_MASK) {
        ESP_LOGI(TAG, "Wrist gesture detected");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_NO_MOT_STATUS_MASK) {
        ESP_LOGI(TAG, "No motion detected");
        interrupt_detected = true;
    }

    // BMI270 implementation note.
    if (int_status & BMI270_ANY_MOT_STATUS_MASK) {
        ESP_LOGI(TAG, "Any motion detected");
        interrupt_detected = true;
    }

    return interrupt_detected;
}

esp_err_t bmi270_tools::clear_interrupt()
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // BMI270 implementation note.
    // BMI270 implementation note.
    uint16_t int_status = 0;
    int8_t rslt = bmi2_get_int_status(&int_status, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to read interrupt status for clearing");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Interrupt status cleared: 0x%04X", int_status);
    return ESP_OK;
}

esp_err_t bmi270_tools::enable_interrupt(int_pin_t pin, bool active_high, bool open_drain, bool latch)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    struct bmi2_int_pin_config pin_config = {};

    // configurationinterrupt pintype
    pin_config.pin_type = pin;
    
    // configurationlatch mode
    pin_config.int_latch = latch ? BMI2_INT_LATCH : BMI2_INT_NON_LATCH;

    // BMI270 implementation note.
    auto configure_pin = [&](uint8_t index) {
        pin_config.pin_cfg[index].input_en = BMI2_INT_INPUT_DISABLE;
        pin_config.pin_cfg[index].lvl = active_high ? BMI2_INT_ACTIVE_HIGH : BMI2_INT_ACTIVE_LOW;
        pin_config.pin_cfg[index].od = open_drain ? BMI2_INT_OPEN_DRAIN : BMI2_INT_PUSH_PULL;
        pin_config.pin_cfg[index].output_en = BMI2_INT_OUTPUT_ENABLE;
    };

    switch (pin) {
        case INT_PIN_1:
            configure_pin(0);
            break;
        case INT_PIN_2:
            configure_pin(1);
            break;
        case INT_PIN_BOTH:
            configure_pin(0);
            configure_pin(1);
            break;
    }

    int8_t rslt = bmi2_set_int_pin_config(&pin_config, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable interrupt pin");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Interrupt enabled on pin %d (active_%s, %s, %s)", 
             pin, active_high ? "high" : "low",
             open_drain ? "open_drain" : "push_pull",
             latch ? "latch" : "non_latch");

    return ESP_OK;
}

esp_err_t bmi270_tools::disable_interrupt(int_pin_t pin)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    struct bmi2_int_pin_config pin_config = {};

    // configurationinterrupt pintype
    pin_config.pin_type = pin;
    pin_config.int_latch = BMI2_INT_NON_LATCH;

    // BMI270 implementation note.
    auto disable_pin = [&](uint8_t index) {
        pin_config.pin_cfg[index].input_en = BMI2_INT_INPUT_DISABLE;
        pin_config.pin_cfg[index].lvl = BMI2_INT_ACTIVE_LOW;
        pin_config.pin_cfg[index].od = BMI2_INT_OPEN_DRAIN;
        pin_config.pin_cfg[index].output_en = BMI2_INT_OUTPUT_DISABLE;  // important:disableoutput
    };

    switch (pin) {
        case INT_PIN_1:
            disable_pin(0);
            break;
        case INT_PIN_2:
            disable_pin(1);
            break;
        case INT_PIN_BOTH:
            disable_pin(0);
            disable_pin(1);
            break;
    }

    int8_t rslt = bmi2_set_int_pin_config(&pin_config, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to disable interrupt pin");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Interrupt disabled on pin %d", pin);
    return ESP_OK;
}

esp_err_t bmi270_tools::map_interrupt_to_pin(uint8_t interrupt_type, int_pin_t pin)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int8_t rslt = BMI2_OK;

    // BMI270 implementation note.
    struct bmi2_sens_int_config sens_int = {
        .type = interrupt_type,
        .hw_int_pin = (enum bmi2_hw_int_pin)pin
    };
    
    // BMI270 implementation note.
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_map_feat_int(&sens_int, 1, &_bmi270_dev);
            break;
            
        case MODE_BASE:
            rslt = bmi270_map_feat_int(&sens_int, 1, &_bmi270_dev);
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for feature interrupt mapping: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to map feature interrupt %d to pin %d", interrupt_type, pin);
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Feature interrupt %d (%s) mapped to pin %d", 
             interrupt_type, 
             (interrupt_type == BMI2_ANY_MOTION) ? "ANY_MOTION" :
             (interrupt_type == BMI2_NO_MOTION) ? "NO_MOTION" :
             (interrupt_type == BMI2_SIG_MOTION) ? "SIG_MOTION" : "OTHER", 
             pin);

    return ESP_OK;
}

esp_err_t bmi270_tools::configure_any_motion(const motion_config_t &config)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Configuring any-motion detection with duration=%u, threshold=%u", config.duration, config.threshold);

    // createsensorconfigurationstructure
    struct bmi2_sens_config sens_config = {0};
    sens_config.type = BMI2_ANY_MOTION;

    // createinterrupt pinconfigurationstructure
    struct bmi2_int_pin_config pin_config = {0};

    int8_t rslt = BMI2_OK;

    // BMI270 implementation note.
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_get_sensor_config(&sens_config, 1, &_bmi270_dev);
            break;
        case MODE_BASE:
            rslt = bmi270_get_sensor_config(&sens_config, 1, &_bmi270_dev);
            break;
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for any-motion configuration: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }

    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get default any-motion configuration");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    // getinterrupt pinconfiguration
    rslt = bmi2_get_int_pin_config(&pin_config, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get interrupt pin configuration");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    // configurationany-motionparameters
    sens_config.cfg.any_motion.duration = config.duration;
    sens_config.cfg.any_motion.threshold = config.threshold;

    // setsensorconfiguration
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_set_sensor_config(&sens_config, 1, &_bmi270_dev);
            break;
        case MODE_BASE:
            rslt = bmi270_set_sensor_config(&sens_config, 1, &_bmi270_dev);
            break;
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for any-motion configuration: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }

    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to set any-motion configuration");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Any-motion configuration applied successfully");
    return ESP_OK;
}

float bmi270_tools::convert_accel_lsb_to_mg(int16_t lsb, accel_range_t range)
{
    float sensitivity = 0.0f;
    switch (range) {
        case ACCEL_RANGE_2G:  sensitivity = 16384.0f; break; // 2^15 / 2g
        case ACCEL_RANGE_4G:  sensitivity = 8192.0f; break;  // 2^15 / 4g
        case ACCEL_RANGE_8G:  sensitivity = 4096.0f; break;  // 2^15 / 8g
        case ACCEL_RANGE_16G: sensitivity = 2048.0f; break;  // 2^15 / 16g
        default: sensitivity = 8192.0f; break;
    }
    return (float)lsb / sensitivity * 1000.0f; // BMI270 implementation note.
}

float bmi270_tools::convert_gyro_lsb_to_dps(int16_t lsb, gyro_range_t range)
{
    float sensitivity = 0.0f;
    switch (range) {
        case GYRO_RANGE_125:  sensitivity = 262.14f; break; // 2^15 / 125
        case GYRO_RANGE_250:  sensitivity = 131.07f; break; // 2^15 / 250
        case GYRO_RANGE_500:  sensitivity = 65.54f; break;  // 2^15 / 500
        case GYRO_RANGE_1000: sensitivity = 32.77f; break;  // 2^15 / 1000
        case GYRO_RANGE_2000: sensitivity = 16.38f; break;  // 2^15 / 2000
        default: sensitivity = 32.77f; break;
    }
    return (float)lsb / sensitivity;
}

esp_err_t bmi270_tools::get_chip_id(uint8_t &chip_id)
{
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data = 0;
    esp_err_t ret = i2c_bus_read_byte(_dev, BMI2_CHIP_ID_ADDR, &data);
    if (ret != ESP_OK) {
        return ret;
    }
    
    chip_id = data;
    return ESP_OK;
}

esp_err_t bmi270_tools::soft_reset()
{
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // BMI270 implementation note.
    int8_t rslt = bmi2_soft_reset(&_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Soft reset failed");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    // BMI270 implementation note.
    const char* mode_name = "";
    switch (_current_mode) {
        case MODE_CONTEXT:
            rslt = bmi270_context_init(&_bmi270_dev);
            mode_name = "Context";
            break;
            
        case MODE_BASE:
            rslt = bmi270_init(&_bmi270_dev);
            mode_name = "Base";
            break;
            
        case MODE_LEGACY:
        case MODE_MAXIMUM_FIFO:
        default:
            ESP_LOGE(TAG, "Unsupported mode for soft reset: %d", _current_mode);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Re-init BMI270 %s after reset failed", mode_name);
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    // restoreconfiguration
    esp_err_t ret = restore_state_after_reset();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "State restore partial failure");
    }

    ESP_LOGI(TAG, "Soft reset completed and %s state restored", mode_name);
    return ESP_OK;
}

esp_err_t bmi270_tools::restore_state_after_reset()
{
    // BMI270 implementation note.
    esp_err_t ret = configure_sensors(_current_config);
    if (ret != ESP_OK) return ret;

    // BMI270 implementation note.
    uint8_t sens_list[3];
    uint8_t cnt = 0;
    if (_enabled_sensors_mask & (1ull << BMI2_ACCEL)) sens_list[cnt++] = BMI2_ACCEL;
    if (_enabled_sensors_mask & (1ull << BMI2_GYRO))  sens_list[cnt++] = BMI2_GYRO;
    if (_enabled_sensors_mask & (1ull << BMI2_AUX))   sens_list[cnt++] = BMI2_AUX;
    if (cnt) {
        // BMI270 implementation note.
        int8_t rslt = BMI2_OK;
        switch (_current_mode) {
            case MODE_CONTEXT:
                rslt = bmi270_context_sensor_enable(sens_list, cnt, &_bmi270_dev);
                break;
                
            case MODE_BASE:
                rslt = bmi270_sensor_enable(sens_list, cnt, &_bmi270_dev);
                break;
                
            case MODE_LEGACY:
            case MODE_MAXIMUM_FIFO:
            default:
                ESP_LOGE(TAG, "Unsupported mode for restore sensors: %d", _current_mode);
                return ESP_ERR_NOT_SUPPORTED;
        }
        
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "Failed to restore sensor enable state");
            print_bmi2_api_error(rslt);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

i2c_bus_device_handle_t bmi270_tools::device() const
{
    return _dev;
}

uint8_t bmi270_tools::address() const
{
    return _addr;
}

bool bmi270_tools::is_initialized() const
{
    return _initialized;
}

// BMI270 implementation note.

int8_t bmi270_tools::bmi2_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if (reg_data == nullptr || len == 0 || intf_ptr == nullptr) {
        ESP_LOGE(TAG, "I2C read param error: data=%p, len=%lu", reg_data, len);
        return -1;
    }

    // BMI270 implementation note.
    if (len > BMI2_FIFO_RAW_DATA_BUFFER_SIZE) {
        ESP_LOGE(TAG, "I2C read length too large: %lu (max: %d)", len, BMI2_FIFO_RAW_DATA_BUFFER_SIZE);
        return -1;
    }

    bmi270_tools *instance = static_cast<bmi270_tools*>(intf_ptr);
    if (!instance->_initialized || instance->_dev == nullptr) {
        ESP_LOGE(TAG, "I2C read state error: initialized=%d, dev=%p", instance->_initialized, instance->_dev);
        return -1;
    }

    // BMI270 implementation note.
    esp_err_t ret = i2c_bus_read_bytes(instance->_dev, reg_addr, len, reg_data);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: reg=0x%02X, len=%lu, err=%s", reg_addr, len, esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

int8_t bmi270_tools::bmi2_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    if (reg_data == nullptr || len == 0 || intf_ptr == nullptr) {
        ESP_LOGE(TAG, "I2C write param error: data=%p, len=%lu", reg_data, len);
        return -1;
    }

    // BMI270 implementation note.
    if (len > BMI2_FIFO_RAW_DATA_BUFFER_SIZE) {
        ESP_LOGE(TAG, "I2C write length too large: %lu (max: %d)", len, BMI2_FIFO_RAW_DATA_BUFFER_SIZE);
        return -1;
    }

    bmi270_tools *instance = static_cast<bmi270_tools*>(intf_ptr);
    if (!instance->_initialized || instance->_dev == nullptr) {
        ESP_LOGE(TAG, "I2C write state error: initialized=%d, dev=%p", instance->_initialized, instance->_dev);
        return -1;
    }

    // BMI270 implementation note.
    esp_err_t ret = i2c_bus_write_bytes(instance->_dev, reg_addr, len, reg_data);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: reg=0x%02X, len=%lu, err=%s", reg_addr, len, esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

void bmi270_tools::delay_us(uint32_t period, void *intf_ptr)
{
    if (period == 0) return;
    
    // BMI270 implementation note.
    if (period >= 10000) {
        uint32_t ms = period / 1000;
        if (ms == 0) ms = 1;
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    
    // BMI270 implementation note.
    esp_rom_delay_us(period);
}

// BMI270 implementation note.
int8_t bmi270_tools::bmi2_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    // BMI270 implementation note.
    ESP_LOGW(TAG, "SPI read not implemented yet: reg=0x%02X, len=%lu", reg_addr, len);
    return BMI2_E_COM_FAIL;
}

// BMI270 implementation note.
int8_t bmi270_tools::bmi2_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    // BMI270 implementation note.
    ESP_LOGW(TAG, "SPI write not implemented yet: reg=0x%02X, len=%lu", reg_addr, len);
    return BMI2_E_COM_FAIL;
}

int8_t bmi270_tools::bmm_i2c_read(uint8_t reg_addr, uint8_t *aux_data, uint32_t len, void *intf_ptr)
{
    if (aux_data == nullptr || len == 0 || intf_ptr == nullptr) {
        ESP_LOGE(TAG, "BMM I2C read param error: aux_data=%p, len=%lu, intf_ptr=%p", aux_data, len, intf_ptr);
        return BMI2_E_NULL_PTR;
    }

    // BMI270 implementation note.
    if (len > 32) {  // BMI270 implementation note.
        ESP_LOGE(TAG, "BMM I2C read length too large: %lu", len);
        return BMI2_E_OUT_OF_RANGE;
    }

    bmi270_tools *instance = static_cast<bmi270_tools*>(intf_ptr);
    if (!instance->_initialized || instance->_dev == nullptr) {
        ESP_LOGE(TAG, "BMM I2C read state error: initialized=%d, dev=%p", instance->_initialized, instance->_dev);
        return BMI2_E_NULL_PTR;
    }

    ESP_LOGD(TAG, "BMM I2C read: reg=0x%02X, len=%lu", reg_addr, len);

    // BMI270 implementation note.
    int8_t rslt = bmi2_read_aux_man_mode(reg_addr, aux_data, (uint16_t)len, &instance->_bmi270_dev);
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMM I2C read failed: reg=0x%02X, len=%lu, rslt=%d", reg_addr, len, rslt);
        return rslt;
    }

    // BMI270 implementation note.
    if (len <= 16) {
        ESP_LOGD(TAG, "BMM I2C read OK: reg=0x%02X, data=[", reg_addr);
        for (uint32_t i = 0; i < len; i++) {
            ESP_LOGD(TAG, "0x%02X%s", aux_data[i], (i < len-1) ? " " : "]");
        }
    } else {
        ESP_LOGD(TAG, "BMM I2C read OK: reg=0x%02X, len=%lu (data not printed due to length)", reg_addr, len);
    }

    return BMI2_OK;
}

int8_t bmi270_tools::bmm_i2c_write(uint8_t reg_addr, const uint8_t *aux_data, uint32_t len, void *intf_ptr)
{
    if (aux_data == nullptr || len == 0 || intf_ptr == nullptr) {
        ESP_LOGE(TAG, "BMM I2C write param error: aux_data=%p, len=%lu, intf_ptr=%p", aux_data, len, intf_ptr);
        return BMI2_E_NULL_PTR;
    }

    // BMI270 implementation note.
    if (len > 32) {  // BMI270 implementation note.
        ESP_LOGE(TAG, "BMM I2C write length too large: %lu", len);
        return BMI2_E_OUT_OF_RANGE;
    }

    bmi270_tools *instance = static_cast<bmi270_tools*>(intf_ptr);
    if (!instance->_initialized || instance->_dev == nullptr) {
        ESP_LOGE(TAG, "BMM I2C write state error: initialized=%d, dev=%p", instance->_initialized, instance->_dev);
        return BMI2_E_NULL_PTR;
    }

    // BMI270 implementation note.
    if (len <= 16) {
        ESP_LOGD(TAG, "BMM I2C write: reg=0x%02X, len=%lu, data=[", reg_addr, len);
        for (uint32_t i = 0; i < len; i++) {
            ESP_LOGD(TAG, "0x%02X%s", aux_data[i], (i < len-1) ? " " : "]");
        }
    } else {
        ESP_LOGD(TAG, "BMM I2C write: reg=0x%02X, len=%lu (data not printed due to length)", reg_addr, len);
    }

    // BMI270 implementation note.
    int8_t rslt = bmi2_write_aux_man_mode(reg_addr, aux_data, (uint16_t)len, &instance->_bmi270_dev);
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMM I2C write failed: reg=0x%02X, len=%lu, rslt=%d", reg_addr, len, rslt);
        return rslt;
    }

    ESP_LOGD(TAG, "BMM I2C write OK: reg=0x%02X, len=%lu", reg_addr, len);
    return BMI2_OK;
}

// BMI270 implementation note.

esp_err_t bmi270_tools::map_feature_interrupt(uint8_t feature_type, int_pin_t pin)
{
    struct bmi2_sens_int_config sens_int = {
        .type = feature_type, 
        .hw_int_pin = static_cast<enum bmi2_hw_int_pin>(pin)
    };
    
    int8_t rslt = bmi270_map_feat_int(&sens_int, 1, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to map feature interrupt");
        print_bmi2_api_error(rslt);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void bmi270_tools::print_bmi2_api_error(int8_t rslt)
{
    switch (rslt) {
        case BMI2_OK:
            break;
        case BMI2_W_FIFO_EMPTY:
            ESP_LOGW(TAG, "Warning [%d]: FIFO empty", rslt);
            break;
        case BMI2_W_PARTIAL_READ:
            ESP_LOGW(TAG, "Warning [%d]: FIFO partial read", rslt);
            break;
        case BMI2_E_NULL_PTR:
            ESP_LOGE(TAG, "Error [%d]: Null pointer error", rslt);
            break;
        case BMI2_E_COM_FAIL:
            ESP_LOGE(TAG, "Error [%d]: Communication failure", rslt);
            break;
        case BMI2_E_DEV_NOT_FOUND:
            ESP_LOGE(TAG, "Error [%d]: Device not found", rslt);
            break;
        case BMI2_E_INVALID_SENSOR:
            ESP_LOGE(TAG, "Error [%d]: Invalid sensor", rslt);
            break;
        case BMI2_E_SELF_TEST_FAIL:
            ESP_LOGE(TAG, "Error [%d]: Self-test failed", rslt);
            break;
        case BMI2_E_INVALID_INT_PIN:
            ESP_LOGE(TAG, "Error [%d]: Invalid interrupt pin", rslt);
            break;
        case BMI2_E_OUT_OF_RANGE:
            ESP_LOGE(TAG, "Error [%d]: Out of range", rslt);
            break;
        case BMI2_E_ACC_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d]: Invalid accelerometer configuration", rslt);
            break;
        case BMI2_E_GYRO_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d]: Invalid gyroscope configuration", rslt);
            break;
        case BMI2_E_CONFIG_LOAD:
            ESP_LOGE(TAG, "Error [%d]: Configuration load error", rslt);
            break;
        case BMI2_E_INVALID_PAGE:
            ESP_LOGE(TAG, "Error [%d]: Invalid page", rslt);
            break;
        case BMI2_E_INVALID_INPUT:
            ESP_LOGE(TAG, "Error [%d]: Invalid input", rslt);
            break;
        case BMI2_E_INVALID_STATUS:
            ESP_LOGE(TAG, "Error [%d]: Invalid status", rslt);
            break;
        case BMI2_E_WRITE_CYCLE_TIMEOUT:
            ESP_LOGE(TAG, "Error [%d]: Write cycle timeout", rslt);
            break;
        case BMI2_E_AUX_INVALID_CFG:
            ESP_LOGE(TAG, "Error [%d]: Auxiliary sensor invalid configuration", rslt);
            break;
        default:
            ESP_LOGE(TAG, "Error [%d]: Unknown error code", rslt);
            break;
    }
}

void bmi270_tools::print_bmm150_api_error(int8_t rslt)
{
    if (rslt != BMM150_OK) {
        switch (rslt) {
            case BMM150_E_NULL_PTR:
                ESP_LOGE(TAG, "Error [%d]: Null pointer error", rslt);
                ESP_LOGE(TAG, "It occurs when the user tries to assign value (not address) to a pointer, which has been initialized to NULL.");
                break;
            case BMM150_E_COM_FAIL:
                ESP_LOGE(TAG, "Error [%d]: Communication failure error", rslt);
                ESP_LOGE(TAG, "It occurs due to read/write operation failure and also due to power failure during communication");
                break;
            case BMM150_E_DEV_NOT_FOUND:
                ESP_LOGE(TAG, "Error [%d]: Device not found error. It occurs when the device chip id is incorrectly read", rslt);
                break;
            case BMM150_E_INVALID_CONFIG:
                ESP_LOGE(TAG, "Error [%d]: Invalid sensor configuration", rslt);
                ESP_LOGE(TAG, "It occurs when there is a mismatch in the requested feature with the available one");
                break;
            default:
                ESP_LOGE(TAG, "Error [%d]: Unknown error code", rslt);
                break;
        }
    }
}

/**
 * @brief configurationBMM150magnetometer
 * See the BMI270 API documentation.
 */
int bmi270_tools::configure_magnetometer() {
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        return BMI2_E_NULL_PTR;
    }

    ESP_LOGI(TAG, "Starting BMM150 magnetometer configuration...");

    int8_t rslt = BMI2_OK;
    _bmm150_dev.chip_id = BMM150_CHIP_ID;
    _bmm150_dev.read = bmm_i2c_read;
    _bmm150_dev.write = bmm_i2c_write;
    _bmm150_dev.delay_us = delay_us;
    _bmm150_dev.intf_ptr = this;
    _bmm150_dev.intf = BMM150_I2C_INTF;

    // BMI270 implementation note.
    struct bmi2_sens_config *config = (struct bmi2_sens_config*)malloc(3 * sizeof(struct bmi2_sens_config));
    if (!config) {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor config");
        return BMI2_E_NULL_PTR;
    }
    
    config[BMI2_ACCEL].type = BMI2_ACCEL;
    config[BMI2_GYRO].type = BMI2_GYRO;
    config[BMI2_AUX].type = BMI2_AUX;
    uint8_t sensor_list[3] = { BMI2_ACCEL, BMI2_GYRO, BMI2_AUX };

    rslt = bmi270_context_get_sensor_config(config, 3, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get sensor config before BMM150 setup: %d", rslt);
        print_bmi2_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "Sensor configuration retrieved for BMM150 setup");
    }

    // BMM150 interfaceinitialization
    ESP_LOGI(TAG, "BMM150 interface configured for auxiliary sensor access");

    /* Configurations for accel. */
    config[BMI2_ACCEL].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    config[BMI2_ACCEL].cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
    config[BMI2_ACCEL].cfg.acc.odr = BMI2_ACC_ODR_50HZ;
    config[BMI2_ACCEL].cfg.acc.range = BMI2_ACC_RANGE_2G;

    /* Configurations for gyro. */
    config[BMI2_GYRO].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    config[BMI2_GYRO].cfg.gyr.noise_perf = BMI2_GYR_RANGE_2000;
    config[BMI2_GYRO].cfg.gyr.bwp = BMI2_GYR_OSR2_MODE;
    config[BMI2_GYRO].cfg.gyr.odr = BMI2_GYR_ODR_50HZ;
    config[BMI2_GYRO].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    config[BMI2_GYRO].cfg.gyr.ois_range = BMI2_GYR_OIS_2000;

    /* Configurations for aux. */
    config[BMI2_AUX].cfg.aux.odr = BMI2_AUX_ODR_50HZ;
    config[BMI2_AUX].cfg.aux.aux_en = BMI2_ENABLE;
    config[BMI2_AUX].cfg.aux.i2c_device_addr = BMM150_DEFAULT_I2C_ADDRESS;
    config[BMI2_AUX].cfg.aux.fcu_write_en = BMI2_ENABLE;
    config[BMI2_AUX].cfg.aux.man_rd_burst = BMI2_AUX_READ_LEN_3;
    config[BMI2_AUX].cfg.aux.read_addr = BMM150_REG_DATA_X_LSB;
    config[BMI2_AUX].cfg.aux.manual_en = BMI2_ENABLE;

    // applyconfiguration
    rslt = bmi270_context_set_sensor_config(config, 3, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to set sensor config for BMM150 setup: %d", rslt);
        print_bmi2_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "Sensor configuration applied for BMM150 setup");
    }

    // BMI270 implementation note.
    rslt = bmi270_context_sensor_enable(sensor_list, 3, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable sensors before BMM150 setup: %d", rslt);
        print_bmi2_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "Base sensors (ACC, GYR, AUX) enabled for BMM150 setup");
    }

    // initializationBMM150
    rslt = bmm150_init(&_bmm150_dev);
    if (rslt != BMM150_OK) {
        ESP_LOGE(TAG, "Failed to initialize BMM150: %d", rslt);
        print_bmm150_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "BMM150 initialized successfully");
    }

    // configurationBMM150 preset_mode parameters
    _bmm150_mag_settings.preset_mode = BMM150_PRESETMODE_ENHANCED;
    rslt = bmm150_set_presetmode(&_bmm150_mag_settings, &_bmm150_dev);
    if (rslt != BMM150_OK) {
        ESP_LOGE(TAG, "Failed to set BMM150 preset mode: %d", rslt);
        print_bmm150_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "BMM150 preset mode set to ENHANCED");
    }

    // configurationBMM150 op_mode parameters
    _bmm150_mag_settings.pwr_mode = BMM150_POWERMODE_NORMAL;
    rslt = bmm150_set_op_mode(&_bmm150_mag_settings, &_bmm150_dev);
    if (rslt != BMM150_OK) {
        ESP_LOGE(TAG, "Failed to set BMM150 power mode: %d", rslt);
        print_bmm150_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "BMM150 power mode set to NORMAL");
    }

    // BMI270 implementation note.
    rslt = bmi270_context_get_sensor_config(config, 3, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to get sensor config after BMM150 setup: %d", rslt);
        print_bmi2_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "Sensor configuration retrieved after BMM150 setup");
    }
    config[BMI2_AUX].cfg.aux.manual_en = BMI2_DISABLE;
    rslt = bmi270_context_set_sensor_config(&config[BMI2_AUX], 1, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to disable AUX manual mode after BMM150 setup: %d", rslt);
        print_bmi2_api_error(rslt);
        free(config);
        return rslt;
    }
    else{
        ESP_LOGI(TAG, "AUX manual mode disabled after BMM150 setup");
    }

    // BMI270 implementation note.
    // rslt = bmi2_map_data_int(BMI2_DRDY_INT, BMI2_INT1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGE(TAG, "Failed to map data ready interrupt for BMM150: %d", rslt);
    //     print_bmi2_api_error(rslt);
    //     free(config);
    //     return rslt;
    // }
    // else{
    //     ESP_LOGI(TAG, "Data ready interrupt mapped to INT1 for BMM150");
    // }
    
    // BMI270 implementation note.
    free(config);

    // BMI270 implementation note.
    rslt = bmi2_set_adv_power_save(BMI2_DISABLE, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGW(TAG, "Failed to enable advanced power save mode: %d", rslt);
        print_bmi2_api_error(rslt);
    } else {
        ESP_LOGI(TAG, "Advanced power save mode enabled");
    }

    // disableFIFO
    rslt = bmi2_set_fifo_config(BMI2_FIFO_ALL_EN, BMI2_DISABLE, &_bmi270_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGW(TAG, "Failed to disable FIFO: %d", rslt);
        print_bmi2_api_error(rslt);
    }
    else{
        ESP_LOGI(TAG, "FIFO disabled before BMM150 setup");
    }

 
//     fifoframe.data = fifo_data;
//     fifoframe.length = BMI2_FIFO_RAW_DATA_USER_LENGTH;

//     /* Set FIFO configuration by enabling accel, gyro and timestamp.
//      * NOTE 1: The header mode is enabled by default.
//      * NOTE 2: By default the FIFO operating mode is in FIFO mode.
//      * NOTE 3: Sensortime is enabled by default */

// BMI270 implementation note.
//     rslt = bmi2_set_fifo_config(BMI2_FIFO_ALL_EN, BMI2_ENABLE, &_bmi270_dev);
//     if (rslt != BMI2_OK) {
//         ESP_LOGW(TAG, "Failed to enable FIFO: %d", rslt);
//         print_bmi2_api_error(rslt);
//     }
//     else{
//         ESP_LOGI(TAG, "FIFO enabled for BMM150 operation");
//     }

//     rslt = bmi2_set_fifo_config(BMI2_FIFO_HEADER_EN, BMI2_DISABLE, &_bmi270_dev);
//     if (rslt != BMI2_OK) {
//         ESP_LOGW(TAG, "Failed to disable FIFO header: %d", rslt);
//         print_bmi2_api_error(rslt);
//     }
//     else{
//         ESP_LOGI(TAG, "FIFO header disabled for BMM150 operation");
//     }

//     fifoframe.data_int_map = BMI2_FFULL_INT;
//     rslt = bmi2_map_data_int(fifoframe.data_int_map, BMI2_INT1, &_bmi270_dev);
//     if (rslt != BMI2_OK) {
//         ESP_LOGW(TAG, "Failed to map FIFO full interrupt: %d", rslt);
//         print_bmi2_api_error(rslt);
//     }
//     else{
//         ESP_LOGI(TAG, "FIFO full interrupt mapped to INT1");
//     }

// BMI270 implementation note.
// #ifdef BMM150_USE_FIXED_POINT
//     ESP_LOGI(TAG, "Magnetometer data contains fraction part (last 4 bits) and decimal part");
// #endif

//     if (_bmm150_dev.chip_id == BMM150_CHIP_ID) {
//         ESP_LOGI(TAG, "Valid BMM150 (Aux) sensor - Chip ID : 0x%x", _bmm150_dev.chip_id);

//         uint16_t int_status = 0;
//         uint16_t accel_frame_length = BMI2_FIFO_ACCEL_FRAME_COUNT;
//         uint16_t gyro_frame_length = BMI2_FIFO_GYRO_FRAME_COUNT;
//         uint16_t aux_frame_length = BMI2_FIFO_AUX_FRAME_COUNT;
//         uint16_t fifo_length = 0;
//         uint16_t index = 0;
//         int8_t try_count = 1;

// BMI270 implementation note.
//         struct bmi2_sens_axes_data fifo_accel_data[BMI2_FIFO_ACCEL_FRAME_COUNT] = { { 0 } };
//         struct bmi2_sens_axes_data fifo_gyro_data[BMI2_FIFO_GYRO_FRAME_COUNT] = { { 0 } };
//         struct bmi2_aux_fifo_data fifo_aux_data[BMI2_FIFO_AUX_FRAME_COUNT] = { { { 0 } } };
//         struct bmm150_mag_data mag_data = {0};

// BMI270 implementation note.
//         ESP_LOGI(TAG, "Waiting for FIFO to accumulate data...");
// BMI270 implementation note.

// BMI270 implementation note.
//             /* Read FIFO data on interrupt. */
//             rslt = bmi2_get_int_status(&int_status, &_bmi270_dev);
//             print_bmi2_api_error(rslt);

//             if ((rslt == BMI2_OK) && (int_status & BMI2_FFULL_INT_STATUS_MASK)) {
//                 ESP_LOGI(TAG, "FIFO processing iteration : %d", try_count);

//                 accel_frame_length = BMI2_FIFO_ACCEL_FRAME_COUNT;
//                 gyro_frame_length = BMI2_FIFO_GYRO_FRAME_COUNT;
//                 aux_frame_length = BMI2_FIFO_AUX_FRAME_COUNT;

//                 rslt = bmi2_get_fifo_length(&fifo_length, &_bmi270_dev);
//                 print_bmi2_api_error(rslt);

//                 /* Updating FIFO length to be read based on available length and dummy byte updation */
//                 fifoframe.length = fifo_length + _bmi270_dev.dummy_byte;

//                 ESP_LOGI(TAG, "FIFO data bytes available : %d", fifo_length);
//                 ESP_LOGI(TAG, "FIFO data bytes requested : %d", fifoframe.length);

//                 /* Read FIFO data. */
//                 rslt = bmi2_read_fifo_data(&fifoframe, &_bmi270_dev);
//                 print_bmi2_api_error(rslt);

//                 /* Read FIFO data on interrupt. */
//                 rslt = bmi2_get_int_status(&int_status, &_bmi270_dev);
//                 print_bmi2_api_error(rslt);

//                 if (rslt == BMI2_OK) {
//                     ESP_LOGI(TAG, "FIFO accel frames requested : %d", accel_frame_length);

//                     /* Parse the FIFO data to extract accelerometer data from the FIFO buffer. */
//                     rslt = bmi2_extract_accel(fifo_accel_data, &accel_frame_length, &fifoframe, &_bmi270_dev);
//                     ESP_LOGI(TAG, "FIFO accel frames extracted : %d", accel_frame_length);

//                     ESP_LOGI(TAG, "FIFO gyro frames requested : %d", gyro_frame_length);

//                     /* Parse the FIFO data to extract gyro data from the FIFO buffer. */
//                     (void)bmi2_extract_gyro(fifo_gyro_data, &gyro_frame_length, &fifoframe, &_bmi270_dev);
//                     ESP_LOGI(TAG, "FIFO gyro frames extracted : %d", gyro_frame_length);

//                     ESP_LOGI(TAG, "FIFO aux frames requested : %d", aux_frame_length);

//                     /* Parse the FIFO data to extract aux data from the FIFO buffer. */
//                     (void)bmi2_extract_aux(fifo_aux_data, &aux_frame_length, &fifoframe, &_bmi270_dev);
//                     ESP_LOGI(TAG, "FIFO aux frames extracted : %d", aux_frame_length);

//                     ESP_LOGI(TAG, "Extracted accel frames");
//                     ESP_LOGI(TAG, "ACCEL_DATA, X, Y, Z");

//                     /* Print the parsed accelerometer data from the FIFO buffer. */
// BMI270 implementation note.
//                         ESP_LOGI(TAG, "%d, %d, %d, %d",
//                                index,
//                                fifo_accel_data[index].x,
//                                fifo_accel_data[index].y,
//                                fifo_accel_data[index].z);
//                     }

//                     ESP_LOGI(TAG, "Extracted gyro frames");
//                     ESP_LOGI(TAG, "GYRO_DATA, X, Y, Z");

//                     /* Print the parsed gyro data from the FIFO buffer. */
// BMI270 implementation note.
//                         ESP_LOGI(TAG, "%d, %d, %d, %d",
//                                index,
//                                fifo_gyro_data[index].x,
//                                fifo_gyro_data[index].y,
//                                fifo_gyro_data[index].z);
//                     }

//                     ESP_LOGI(TAG, "Extracted AUX frames");
//                     ESP_LOGI(TAG, "AUX_DATA, Mag_uT_X, Mag_uT_Y, Mag_uT_Z");

//                     /* Print the parsed aux data from the FIFO buffer. */
// BMI270 implementation note.
//                         /* Compensating the raw auxiliary data available from the BMM150 API. */
//                         rslt = bmm150_aux_mag_data(fifo_aux_data[index].data, &mag_data, &_bmm150_dev);
//                         print_bmm150_api_error(rslt);

//                         ESP_LOGI(TAG, "%d, %ld, %ld, %ld",
//                                index,
//                                (long int)mag_data.x,
//                                (long int)mag_data.y,
//                                (long int)mag_data.z);
//                     }

//                     try_count++;
//                 }
//             } else {
//                 ESP_LOGD(TAG, "FIFO not full yet (try %d/%d), waiting for more data...", try_count, 10);
// BMI270 implementation note.
//                 vTaskDelay(pdMS_TO_TICKS(500)); // wait 500ms
//                 try_count++;
//             }
//         }
        
//         ESP_LOGI(TAG, "FIFO processing completed after %d attempts", try_count - 1);
//     } else {
//         ESP_LOGW(TAG, "Invalid BMM150 (Aux) sensor - Chip ID : 0x%x", _bmm150_dev.chip_id);
//     }


    // BMI270 implementation note.
    // uint8_t sens_list = BMI2_AUX;
    // rslt = bmi2_sensor_enable(&sens_list, 1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGE(TAG, "Failed to enable BMM150 aux sensor: %d", rslt);
    //     print_bmi2_api_error(rslt);
    //     return rslt;
    // }

    // BMI270 implementation note.
    // vTaskDelay(pdMS_TO_TICKS(20));

    // BMI270 implementation note.
    // uint8_t bmm150_power = 0x01; // Normal power on
    // rslt = bmi2_write_aux_man_mode(0x4B, &bmm150_power, 1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGE(TAG, "Failed to pre-power BMM150: %d", rslt);
    //     print_bmi2_api_error(rslt);
    //     return rslt;
    // }
    // BMI270 implementation note.

    // uint8_t bmm150_chip_id = 0;
    // const uint8_t primary_addr = 0x10;
    // BMI270 implementation note.
    // bool id_ok = false;
    // for (int attempt = 0; attempt < 4 && !id_ok; ++attempt) {
    //     rslt = bmi2_read_aux_man_mode(0x40, &bmm150_chip_id, 1, &_bmi270_dev);
    //     if (rslt == BMI2_OK && bmm150_chip_id == 0x32) {
    //         id_ok = true;
    //         ESP_LOGI(TAG, "BMM150 Chip ID OK (0x32) after attempt %d @addr 0x%02X", attempt+1, aux_cfg.cfg.aux.i2c_device_addr);
    //         break;
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(5));
    //     ESP_LOGW(TAG, "BMM150 Chip ID read attempt %d failed: rslt=%d id=0x%02X", attempt+1, rslt, bmm150_chip_id);
    // BMI270 implementation note.
    //     if (attempt == 1 && aux_cfg.cfg.aux.i2c_device_addr == primary_addr) {
    //         ESP_LOGW(TAG, "Switching AUX to secondary address 0x%02X and retry", secondary_addr);
    //         aux_cfg.cfg.aux.i2c_device_addr = secondary_addr;
    //         int8_t set_ret = bmi2_set_sensor_config(&aux_cfg, 1, &_bmi270_dev);
    //         if (set_ret != BMI2_OK) {
    //             ESP_LOGW(TAG, "Switch to secondary addr set_config failed %d", set_ret);
    //         }
    // BMI270 implementation note.
    //         uint8_t sens_tmp = BMI2_AUX; bmi2_sensor_enable(&sens_tmp, 1, &_bmi270_dev);
    //         vTaskDelay(pdMS_TO_TICKS(5));
    //     }
    // }
    // if (!id_ok) {
    //     ESP_LOGW(TAG, "BMM150 Chip ID not detected (last id=0x%02X) - abort AUX setup", bmm150_chip_id);
    //     return BMI2_E_DEV_NOT_FOUND;
    // }

    // BMI270 implementation note.
    // vTaskDelay(pdMS_TO_TICKS(4));

    // BMI270 implementation note.
    // BMI270 implementation note.
    // BMI270 implementation note.

    // rslt = bmi2_write_aux_man_mode(0x51, &bmm150_rep_xy, 1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGW(TAG, "Set BMM150 REP_XY fail: %d", rslt);
    // }
    // rslt = bmi2_write_aux_man_mode(0x52, &bmm150_rep_z, 1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGW(TAG, "Set BMM150 REP_Z fail: %d", rslt);
    // }

    // BMI270 implementation note.
    // BMI270 implementation note.
    // vTaskDelay(pdMS_TO_TICKS(4));
    // BMI270 implementation note.
    // rslt = bmi2_write_aux_man_mode(0x4C, &bmm150_mode, 1, &_bmi270_dev);
    // if (rslt != BMI2_OK) {
    //     ESP_LOGE(TAG, "Failed to set BMM150 operation mode: %d", rslt);
    //     print_bmi2_api_error(rslt);
    //     return rslt;
    // }
    // vTaskDelay(pdMS_TO_TICKS(6));

    // BMI270 implementation note.
    // bmm150_chip_id = 0;
    // rslt = bmi2_read_aux_man_mode(0x40, &bmm150_chip_id, 1, &_bmi270_dev);
    // if (rslt == BMI2_OK) {
    //     ESP_LOGI(TAG, "BMM150 final verification - Chip ID: 0x%02X", bmm150_chip_id);
    // } else {
    //     ESP_LOGW(TAG, "BMM150 final verification failed: %d", rslt);
    // }

    // BMI270 implementation note.
    // struct bmi2_sens_config verify_aux_cfg;
    // verify_aux_cfg.type = BMI2_AUX;
    // rslt = bmi2_get_sensor_config(&verify_aux_cfg, 1, &_bmi270_dev);
    // if (rslt == BMI2_OK) {
    // ESP_LOGI(TAG, "AUX config verification - ODR: %d, enabled: %d, addr: 0x%02X, manual_en: %d", 
    //      verify_aux_cfg.cfg.aux.odr, 
    //      verify_aux_cfg.cfg.aux.aux_en,
    //      verify_aux_cfg.cfg.aux.i2c_device_addr,
    //      verify_aux_cfg.cfg.aux.manual_en);
    // }

    // BMI270 implementation note.
    // uint8_t trim_reg = 0;
    // int8_t trim_rslt = bmi2_get_regs(BMI2_AUX_IF_TRIM, &trim_reg, 1, &_bmi270_dev);
    // if (trim_rslt == BMI2_OK) {
    //     ESP_LOGI(TAG, "AUX_IF_TRIM(0x68)=0x%02X (expect 0x02 for 10k)", trim_reg);
    // } else {
    //     ESP_LOGW(TAG, "Read AUX_IF_TRIM failed %d", trim_rslt);
    // }

    // BMI270 implementation note.
    // uint8_t dbg_buf[2] = {0};
    // if (bmi2_read_aux_man_mode(0x4B, &dbg_buf[0], 1, &_bmi270_dev) == BMI2_OK &&
    //     bmi2_read_aux_man_mode(0x4C, &dbg_buf[1], 1, &_bmi270_dev) == BMI2_OK) {
    //     ESP_LOGI(TAG, "BMM150 PWR_CTRL(0x4B)=0x%02X, OPR_MODE(0x4C)=0x%02X", dbg_buf[0], dbg_buf[1]);
    // }

    // // 9. checksensorenablestatus
    // uint8_t sensor_status = 0;
    // rslt = bmi2_get_internal_status(&sensor_status, &_bmi270_dev);
    // if (rslt == BMI2_OK) {
    //     ESP_LOGI(TAG, "BMI270 internal status: 0x%02X", sensor_status);
    // }

    // BMI270 implementation note.
    // rslt = bmi2_get_sensor_config(&verify_aux_cfg, 1, &_bmi270_dev);
    // if (rslt == BMI2_OK) {
    //     if (verify_aux_cfg.cfg.aux.manual_en == BMI2_ENABLE) {
    //         verify_aux_cfg.cfg.aux.manual_en = BMI2_DISABLE;
    //         rslt = bmi2_set_sensor_config(&verify_aux_cfg, 1, &_bmi270_dev);
    //         if (rslt == BMI2_OK) {
    //             ESP_LOGI(TAG, "AUX manual mode -> data mode (manual_en DISABLE)");
    //         } else {
    //             ESP_LOGW(TAG, "Disable manual_en failed: %d", rslt);
    //         }
    //     }
    // } else {
    //     ESP_LOGW(TAG, "Re-get AUX config before disabling manual_en failed: %d", rslt);
    // }

    // BMI270 implementation note.
    // vTaskDelay(pdMS_TO_TICKS(30));

    // BMI270 implementation note.
    // int8_t map_rslt = bmi2_map_data_int(BMI2_DRDY_INT, BMI2_INT1, &_bmi270_dev);
    // if (map_rslt != BMI2_OK) {
    //     ESP_LOGW(TAG, "Map data ready int failed: %d", map_rslt);
    // }

    // BMI270 implementation note.
    // bool drdy_ok = false;
    // for (int i = 0; i < 20; ++i) {
    //     struct bmi2_sens_data tmp = {0};
    //     if (bmi2_get_sensor_data(&tmp, &_bmi270_dev) == BMI2_OK) {
    //         if (tmp.status & BMI2_DRDY_AUX) { drdy_ok = true; break; }
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(10));
    // }
    // ESP_LOGI(TAG, "AUX auto mode DRDY %s", drdy_ok ? "READY" : "TIMEOUT");
    // if (!drdy_ok) {
    //     ESP_LOGW(TAG, "AUX DRDY not seen; fallback path will be used until DRDY appears");
    // }

    // ESP_LOGI(TAG, "BMM150 magnetometer configured successfully (25Hz ODR, Low Power preset)");
    return BMI2_OK;
}

/**
 * @brief readBMM150magnetometerdata
 * @param mag_data magnetometerdatastructurepointer
 * See the BMI270 API documentation.
 */
int bmi270_tools::read_magnetometer_data(mag_data_t* mag_data) {
    if (!_initialized || !mag_data) {
        ESP_LOGE(TAG, "BMI270 not initialized or invalid parameter");
        return BMI2_E_NULL_PTR;
    }

    int8_t rslt = BMI2_OK;
    struct bmi2_sens_data sensor_data = {0};
    
    // readauxiliarysensordata
    rslt = bmi2_get_sensor_data(&sensor_data, &_bmi270_dev);
    
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to read BMM150 data: %d", rslt);
        print_bmi2_api_error(rslt);
        mag_data->valid = false;
        return rslt;
    }

    // BMI270 implementation note.
    ESP_LOGI(TAG, "BMM150 status register: 0x%02X, DRDY_AUX: %s", 
             sensor_data.status, 
             (sensor_data.status & BMI2_DRDY_AUX) ? "READY" : "NOT_READY");

    // BMI270 implementation note.
    if (sensor_data.status & BMI2_DRDY_AUX) {
        // BMI270 implementation note.
        ESP_LOGI(TAG, "BMM150 raw aux_data: [0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X]",
                 sensor_data.aux_data[0], sensor_data.aux_data[1], sensor_data.aux_data[2], sensor_data.aux_data[3],
                 sensor_data.aux_data[4], sensor_data.aux_data[5], sensor_data.aux_data[6], sensor_data.aux_data[7]);

        // BMI270 implementation note.
        // BMI270 implementation note.
        uint8_t bmm150_status = sensor_data.aux_data[6];
        ESP_LOGI(TAG, "BMM150 internal status: 0x%02X", bmm150_status);

        // BMI270 implementation note.
        int16_t raw_x = (int16_t)((sensor_data.aux_data[1] << 8) | sensor_data.aux_data[0]);
        int16_t raw_y = (int16_t)((sensor_data.aux_data[3] << 8) | sensor_data.aux_data[2]);
        int16_t raw_z = (int16_t)((sensor_data.aux_data[5] << 8) | sensor_data.aux_data[4]);

        ESP_LOGI(TAG, "BMM150 raw values: X=%d, Y=%d, Z=%d", raw_x, raw_y, raw_z);

        // BMI270 implementation note.
        // BMI270 implementation note.
        // BMI270 implementation note.
        raw_x = raw_x >> 3;
        raw_y = raw_y >> 3;
        raw_z = raw_z >> 1;

        // BMI270 implementation note.
        bool overflow_x = (sensor_data.aux_data[0] & 0x01) != 0;
        bool overflow_y = (sensor_data.aux_data[2] & 0x01) != 0;
        bool overflow_z = (sensor_data.aux_data[4] & 0x02) != 0;

        if (overflow_x || overflow_y || overflow_z) {
            ESP_LOGW(TAG, "BMM150 data overflow detected: X=%d Y=%d Z=%d", overflow_x, overflow_y, overflow_z);
        }

        // BMI270 implementation note.
        // BMM150defaultrange: ±1300uT
        // BMI270 implementation note.
        // BMI270 implementation note.
        mag_data->x = (int32_t)(raw_x * 0.32f);
        mag_data->y = (int32_t)(raw_y * 0.32f);
        mag_data->z = (int32_t)(raw_z * 0.15f);
        mag_data->valid = true;

        ESP_LOGI(TAG, "BMM150 data: X=%ld uT, Y=%ld uT, Z=%ld uT", 
                 mag_data->x, mag_data->y, mag_data->z);
    } 
    // else {
    // BMI270 implementation note.
    //     ESP_LOGI(TAG, "BMM150 data not ready, status=0x%02X -> fallback manual probe", sensor_data.status);
    //     mag_data->valid = false;
    //     struct bmi2_sens_config aux_cfg = {0};
    //     aux_cfg.type = BMI2_AUX;
    //     if (bmi2_get_sensor_config(&aux_cfg, 1, &_bmi270_dev) == BMI2_OK) {
    //         bool need_restore = false;
    //         if (aux_cfg.cfg.aux.manual_en == BMI2_DISABLE) {
    //             aux_cfg.cfg.aux.manual_en = BMI2_ENABLE;
    //             if (bmi2_set_sensor_config(&aux_cfg, 1, &_bmi270_dev) == BMI2_OK) {
    //                 need_restore = true;
    //                 vTaskDelay(pdMS_TO_TICKS(2));
    //             } else {
    //                 ESP_LOGW(TAG, "Enable manual for fallback failed");
    //             }
    //         }
    //         uint8_t buf[8] = {0};
    //         if (bmi2_read_aux_man_mode(0x42, buf, 8, &_bmi270_dev) == BMI2_OK) {
    //             ESP_LOGI(TAG, "Manual aux block: [%02X %02X %02X %02X %02X %02X %02X %02X]", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
    //             auto se = [](int32_t v,int bits){int32_t m=1<<(bits-1);return (v^m)-m;};
    //             int32_t x13 = se(((int32_t)buf[1]<<5)|(buf[0]>>3),13);
    //             int32_t y13 = se(((int32_t)buf[3]<<5)|(buf[2]>>3),13);
    //             int32_t z15 = se(((int32_t)buf[5]<<7)|(buf[4]>>1),15);
    //             mag_data->x = (int32_t)(x13 * 3 / 10); // ≈0.3uT/LSB
    //             mag_data->y = (int32_t)(y13 * 3 / 10);
    //             mag_data->z = (int32_t)(z15 * 15 / 100); // 0.15uT/LSB
    // BMI270 implementation note.
    //             ESP_LOGI(TAG, "Fallback mag approx(uT): X=%ld Y=%ld Z=%ld", mag_data->x, mag_data->y, mag_data->z);
    //         } else {
    //             ESP_LOGW(TAG, "Manual aux block read failed");
    //         }
    //         if (need_restore) {
    //             aux_cfg.cfg.aux.manual_en = BMI2_DISABLE;
    //             if (bmi2_set_sensor_config(&aux_cfg, 1, &_bmi270_dev) != BMI2_OK) {
    //                 ESP_LOGW(TAG, "Restore manual_en disable failed");
    //             }
    //         }
    //     }
    // }

    return BMI2_OK;
}

/*!
 * @brief This function converts lsb to meter per second squared for 16 bit accelerometer at
 * range 2G, 4G, 8G or 16G.
 */
float  bmi270_tools::lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width)
{
    double power = 2;

    float half_scale = (float)((pow((double)power, (double)bit_width) / 2.0f));

    return (GRAVITY_EARTH * val * g_range) / half_scale;
}

// BMI270 implementation note.

esp_err_t bmi270_tools::get_quaternion_data(quaternion_data_t &quat_data)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 NOT initialized");
        quat_data.valid = false;
        return ESP_FAIL;
    }

    // readsensordata
    sensor_data_t sensor_data;
    esp_err_t rslt = get_sensor_data(sensor_data);
    
    if (rslt != ESP_OK || !sensor_data.acc_valid) {
        ESP_LOGE(TAG, "Failed to read sensor data for quaternion calculation");
        quat_data.valid = false;
        return ESP_FAIL;
    }

    // BMI270 implementation note.
    float ax = sensor_data.acc_x / GRAVITY_EARTH;  // BMI270 implementation note.
    float ay = sensor_data.acc_y / GRAVITY_EARTH;
    float az = sensor_data.acc_z / GRAVITY_EARTH;
    
    // BMI270 implementation note.
    if (ax == 0.0f && ay == 0.0f && az == 0.0f) {
        ESP_LOGW(TAG, "Invalid accelerometer data, cannot calculate quaternion");
        quat_data.valid = false;
        return ESP_FAIL;
    }
    
    // BMI270 implementation note.
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm < 1e-6f) {
        ESP_LOGW(TAG, "Accelerometer norm too small, cannot calculate quaternion");
        quat_data.valid = false;
        return ESP_FAIL;
    }
    
    ax /= norm;
    ay /= norm;
    az /= norm;
    
    // BMI270 implementation note.
    // BMI270 implementation note.
    
    // BMI270 implementation note.
    float roll = atan2f(ay, az);
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    
    // BMI270 implementation note.
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cy = 1.0f;  // cos(yaw/2) = cos(0) = 1
    float sy = 0.0f;  // sin(yaw/2) = sin(0) = 0
    
    // quaternioncalculate:q = qyaw * qpitch * qroll
    quat_data.w = cr * cp * cy + sr * sp * sy;
    quat_data.x = sr * cp * cy - cr * sp * sy;
    quat_data.y = cr * sp * cy + sr * cp * sy;
    quat_data.z = cr * cp * sy - sr * sp * cy;
    
    // normalizequaternion
    float q_norm = sqrtf(quat_data.w * quat_data.w + quat_data.x * quat_data.x + 
                        quat_data.y * quat_data.y + quat_data.z * quat_data.z);
    
    if (q_norm < 1e-6f) {
        ESP_LOGW(TAG, "Quaternion norm too small");
        quat_data.valid = false;
        return ESP_FAIL;
    }
    
    quat_data.w /= q_norm;
    quat_data.x /= q_norm;
    quat_data.y /= q_norm;
    quat_data.z /= q_norm;
    quat_data.valid = true;
    
    ESP_LOGD(TAG, "Calculated quaternion: w=%.3f, x=%.3f, y=%.3f, z=%.3f", 
             quat_data.w, quat_data.x, quat_data.y, quat_data.z);
    
    return ESP_OK;
}

bmi270_tools::absolute_orientation_t bmi270_tools::quaternion_to_xyz_rotation(const quaternion_data_t &quat)
{
    absolute_orientation_t xyz_rotation;
    
    if (!quat.valid) {
        xyz_rotation.x_rotation = xyz_rotation.y_rotation = xyz_rotation.z_rotation = 0.0f;
        xyz_rotation.valid = false;
        return xyz_rotation;
    }
    
    float x = quat.x, y = quat.y, z = quat.z, w = quat.w;
    
    // BMI270 implementation note.
    
    // BMI270 implementation note.
    float sinr_cosp = 2.0f * (w * x + y * z);
    float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
    xyz_rotation.x_rotation = atan2f(sinr_cosp, cosr_cosp) * 180.0f / (float)M_PI;
    
    // BMI270 implementation note.
    float sinp = 2.0f * (w * y - z * x);
    // BMI270 implementation note.
    if (sinp >= 1.0f) {
        xyz_rotation.y_rotation = 90.0f;  // BMI270 implementation note.
    } else if (sinp <= -1.0f) {
        xyz_rotation.y_rotation = -90.0f;
    } else {
        xyz_rotation.y_rotation = asinf(sinp) * 180.0f / (float)M_PI;
    }
    
    // BMI270 implementation note.
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    xyz_rotation.z_rotation = atan2f(siny_cosp, cosy_cosp) * 180.0f / (float)M_PI;
    
    xyz_rotation.valid = true;
    return xyz_rotation;
}

esp_err_t bmi270_tools::get_absolute_orientation(absolute_orientation_t &orientation)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        orientation.valid = false;
        return ESP_FAIL;
    }

    // BMI270 implementation note.
    sensor_data_t raw_data;
    if (get_sensor_data(raw_data) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sensor data");
        orientation.valid = false;
        return ESP_FAIL;
    }

    // BMI270 implementation note.
    if (!validate_sensor_data(raw_data)) {
        ESP_LOGW(TAG, "Invalid sensor data detected");
        // BMI270 implementation note.
        if (_last_orientation.valid) {
            orientation = _last_orientation;
            return ESP_OK;
        } else {
            orientation.valid = false;
            return ESP_FAIL;
        }
    }

    // applysensorcalibration
    sensor_data_t calibrated_data = raw_data;
    apply_sensor_calibration(calibrated_data);

    // BMI270 implementation note.
    update_motion_detector(calibrated_data);
    
    // BMI270 implementation note.
    update_gyro_bias_calibration(calibrated_data);
    
    // applygyroscopebiascorrection
    apply_gyro_bias_correction(calibrated_data);

    // applylow-pass filtersmoothingsensordata
    apply_low_pass_filter_3d(_filters.acc_filter, calibrated_data.acc_x, calibrated_data.acc_y, calibrated_data.acc_z);
    apply_low_pass_filter_3d(_filters.gyro_filter, calibrated_data.gyr_x, calibrated_data.gyr_y, calibrated_data.gyr_z);
    
    bool mag_available = is_magnetometer_enabled() && calibrated_data.mag_valid;
    if (mag_available) {
        float mag_x = (float)calibrated_data.mag_x;
        float mag_y = (float)calibrated_data.mag_y;
        float mag_z = (float)calibrated_data.mag_z;
        apply_low_pass_filter_3d(_filters.mag_filter, mag_x, mag_y, mag_z);
        calibrated_data.mag_x = (int16_t)mag_x;
        calibrated_data.mag_y = (int16_t)mag_y;
        calibrated_data.mag_z = (int16_t)mag_z;
    }

    // BMI270 implementation note.
    bool is_stationary = is_device_stationary();
    
    // BMI270 implementation note.
    float gyro_norm_deg = 0.0f;  // BMI270 implementation note.
    
    // BMI270 implementation note.
    if (calibrated_data.gyr_valid) {
        // initializationMadgwickfilter
        if (!_madgwick_filter.initialized) {
            _madgwick_filter.beta = 0.1f;  // BMI270 implementation note.
            _madgwick_filter.adaptive_gain = 0.1f;
            _madgwick_filter.q0 = 1.0f; 
            _madgwick_filter.q1 = 0.0f; 
            _madgwick_filter.q2 = 0.0f; 
            _madgwick_filter.q3 = 0.0f;
            _madgwick_filter.last_update_ms = esp_timer_get_time() / 1000;
            _madgwick_filter.mag_norm_ref = 50.0f;  // BMI270 implementation note.
            _madgwick_filter.stable_count = 0;
            _madgwick_filter.initialized = true;
            ESP_LOGI(TAG, "Madgwick filter initialized with drift compensation");
        }

        // BMI270 implementation note.
        uint32_t now_ms = esp_timer_get_time() / 1000;
        float dt = (now_ms - _madgwick_filter.last_update_ms) / 1000.0f;
        _madgwick_filter.last_update_ms = now_ms;
        
        // BMI270 implementation note.
        if (dt > 0.1f) dt = 0.01f;  // BMI270 implementation note.
        if (dt < 0.001f) dt = 0.001f;

        // unit conversion
        float gx = calibrated_data.gyr_x * (float)M_PI / 180.0f;  // BMI270 implementation note.
        float gy = calibrated_data.gyr_y * (float)M_PI / 180.0f;
        float gz = calibrated_data.gyr_z * (float)M_PI / 180.0f;
        float ax = calibrated_data.acc_x / GRAVITY_EARTH;  // BMI270 implementation note.
        float ay = calibrated_data.acc_y / GRAVITY_EARTH;
        float az = calibrated_data.acc_z / GRAVITY_EARTH;

        // BMI270 implementation note.
        float gyro_norm = sqrtf(gx*gx + gy*gy + gz*gz) * 180.0f / (float)M_PI; // BMI270 implementation note.
        float acc_norm = sqrtf(ax*ax + ay*ay + az*az);
        gyro_norm_deg = gyro_norm; // BMI270 implementation note.

        // BMI270 implementation note.
        if (is_stationary) {
            // BMI270 implementation note.
            gx *= 0.05f;  // BMI270 implementation note.
            gy *= 0.05f;
            gz *= 0.05f;
            _madgwick_filter.beta = 1.5f;  // BMI270 implementation note.
            _madgwick_filter.stable_count++;
        } else {
            // BMI270 implementation note.
            float motion_intensity = gyro_norm;  // BMI270 implementation note.
            if (motion_intensity < 10.0f) {
                // BMI270 implementation note.
                _madgwick_filter.beta = 0.5f;
            } else if (motion_intensity < 50.0f) {
                // BMI270 implementation note.
                _madgwick_filter.beta = 0.3f;
            } else {
                // BMI270 implementation note.
                _madgwick_filter.beta = 0.1f;
            }
            _madgwick_filter.stable_count = 0;
        }

        // calculateadaptive gain
        float mag_norm = 0.0f;
        
        if (mag_available) {
            float mx = (float)calibrated_data.mag_x;
            float my = (float)calibrated_data.mag_y;
            float mz = (float)calibrated_data.mag_z;
            mag_norm = sqrtf(mx*mx + my*my + mz*mz);
            
            // magnetometerdatanormalize
            if (mag_norm > 1e-3f) {
                mx /= mag_norm; my /= mag_norm; mz /= mag_norm;
            }
            
            // BMI270 implementation note.
            madgwick_update_9dof(gx, gy, gz, ax, ay, az, mx, my, mz, dt);
        } else {
            // BMI270 implementation note.
            madgwick_update_6dof(gx, gy, gz, ax, ay, az, dt);
        }

        // BMI270 implementation note.
        quaternion_data_t quat_data;
        quat_data.w = _madgwick_filter.q0;
        quat_data.x = _madgwick_filter.q1;
        quat_data.y = _madgwick_filter.q2;
        quat_data.z = _madgwick_filter.q3;
        quat_data.valid = true;
        
        // BMI270 implementation note.
        apply_quaternion_filter(quat_data.w, quat_data.x, quat_data.y, quat_data.z, is_stationary);
        
        // BMI270 implementation note.
        orientation = quaternion_to_xyz_rotation_stable(quat_data);
        
        // BMI270 implementation note.
        if (_last_orientation.valid) {
            // BMI270 implementation note.
            float smooth_factor = is_stationary ? 0.9f : 0.7f;
            
            // BMI270 implementation note.
            orientation.x_rotation = smooth_angle_transition(
                orientation.x_rotation, 
                _last_orientation.x_rotation, 
                smooth_factor
            );
            orientation.y_rotation = smooth_angle_transition(
                orientation.y_rotation, 
                _last_orientation.y_rotation, 
                smooth_factor
            );
            orientation.z_rotation = smooth_angle_transition(
                orientation.z_rotation, 
                _last_orientation.z_rotation, 
                smooth_factor
            );
        }
        
        // BMI270 implementation note.
        if (is_stationary && _madgwick_filter.stable_count > 100) {  // BMI270 implementation note.
            ESP_LOGI(TAG, "Device stationary for extended period, resetting orientation drift");
            reset_orientation_drift();
            _madgwick_filter.stable_count = 0;
        }
        
        // BMI270 implementation note.
        if (_last_orientation.valid) {
            float roll_diff = fabsf(angle_difference(orientation.x_rotation, _last_orientation.x_rotation));
            float pitch_diff = fabsf(angle_difference(orientation.y_rotation, _last_orientation.y_rotation));
            float yaw_diff = fabsf(angle_difference(orientation.z_rotation, _last_orientation.z_rotation));
            
            // BMI270 implementation note.
            float gyro_norm = sqrtf(gx*gx + gy*gy + gz*gz) * 180.0f / (float)M_PI; // BMI270 implementation note.
            float base_threshold = is_stationary ? 8.0f : 40.0f; // BMI270 implementation note.
            float dynamic_threshold = base_threshold + gyro_norm * 0.3f; // BMI270 implementation note.
            
            // BMI270 implementation note.
            if (dynamic_threshold > 80.0f) dynamic_threshold = 80.0f;
            if (dynamic_threshold < 8.0f) dynamic_threshold = 8.0f;
            
            // BMI270 implementation note.
            bool extreme_change = (roll_diff > 120.0f || pitch_diff > 120.0f || yaw_diff > 120.0f);
            
            if (extreme_change) {
                ESP_LOGW(TAG, "Extreme angle change detected: roll=%.1f°, pitch=%.1f°, yaw=%.1f°", 
                         roll_diff, pitch_diff, yaw_diff);
                
                // BMI270 implementation note.
                float blend_factor = is_stationary ? 0.2f : 0.7f;
                orientation.x_rotation = _last_orientation.x_rotation + 
                    angle_difference(orientation.x_rotation, _last_orientation.x_rotation) * blend_factor;
                orientation.y_rotation = _last_orientation.y_rotation + 
                    angle_difference(orientation.y_rotation, _last_orientation.y_rotation) * blend_factor;
                orientation.z_rotation = _last_orientation.z_rotation + 
                    angle_difference(orientation.z_rotation, _last_orientation.z_rotation) * blend_factor;
            } else if (roll_diff > dynamic_threshold || pitch_diff > dynamic_threshold || yaw_diff > dynamic_threshold) {
                // BMI270 implementation note.
                ESP_LOGD(TAG, "Large angle change: roll=%.1f°, pitch=%.1f°, yaw=%.1f° (threshold=%.1f°)", 
                         roll_diff, pitch_diff, yaw_diff, dynamic_threshold);
            }
        }
        
        // BMI270 implementation note.
        float angle_alpha = is_stationary ? 0.95f : 0.8f;
        apply_low_pass_filter_1d(_filters.angle_filter.x, orientation.x_rotation, angle_alpha);
        apply_low_pass_filter_1d(_filters.angle_filter.y, orientation.y_rotation, angle_alpha);
        apply_low_pass_filter_1d(_filters.angle_filter.z, orientation.z_rotation, angle_alpha);
        
        orientation.x_rotation = _filters.angle_filter.x;
        orientation.y_rotation = _filters.angle_filter.y;
        orientation.z_rotation = _filters.angle_filter.z;
        
    } else {
        // BMI270 implementation note.
        ESP_LOGW(TAG, "Gyroscope not available, using static calculation");
        
        float ax = calibrated_data.acc_x / GRAVITY_EARTH;
        float ay = calibrated_data.acc_y / GRAVITY_EARTH;
        float az = calibrated_data.acc_z / GRAVITY_EARTH;
        float acc_norm = sqrtf(ax*ax + ay*ay + az*az);
        
        if (acc_norm > 0.5f && acc_norm < 1.5f) { // BMI270 implementation note.
            // normalize
            ax /= acc_norm; ay /= acc_norm; az /= acc_norm;
            
            // BMI270 implementation note.
            orientation.x_rotation = atan2f(ay, az) * 180.0f / (float)M_PI;
            orientation.y_rotation = atan2f(-ax, sqrtf(ay*ay + az*az)) * 180.0f / (float)M_PI;
            
            // BMI270 implementation note.
            if (mag_available) {
                float mx = (float)calibrated_data.mag_x;
                float my = (float)calibrated_data.mag_y;
                float mz = (float)calibrated_data.mag_z;
                float mag_norm = sqrtf(mx*mx + my*my + mz*mz);
                
                if (mag_norm > 1e-3f) {
                    mx /= mag_norm; my /= mag_norm; mz /= mag_norm;
                    
                    // BMI270 implementation note.
                    float roll_rad = orientation.x_rotation * (float)M_PI / 180.0f;
                    float pitch_rad = orientation.y_rotation * (float)M_PI / 180.0f;
                    float cos_r = cosf(roll_rad), sin_r = sinf(roll_rad);
                    float cos_p = cosf(pitch_rad), sin_p = sinf(pitch_rad);
                    
                    float mx_comp = mx * cos_p + mz * sin_p;
                    float my_comp = mx * sin_r * sin_p + my * cos_r - mz * sin_r * cos_p;
                    
                    orientation.z_rotation = atan2f(-my_comp, mx_comp) * 180.0f / (float)M_PI;
                } else {
                    orientation.z_rotation = 0.0f;
                }
            } else {
                orientation.z_rotation = 0.0f;
            }
            
            // applyanglefiltering
            if (_filters.angle_filter.initialized) {
                apply_low_pass_filter_1d(_filters.angle_filter.x, orientation.x_rotation, 0.9f);
                apply_low_pass_filter_1d(_filters.angle_filter.y, orientation.y_rotation, 0.9f);
                apply_low_pass_filter_1d(_filters.angle_filter.z, orientation.z_rotation, 0.9f);
                
                orientation.x_rotation = _filters.angle_filter.x;
                orientation.y_rotation = _filters.angle_filter.y;
                orientation.z_rotation = _filters.angle_filter.z;
            }
        } else {
            ESP_LOGW(TAG, "Accelerometer data suggests non-gravitational acceleration (norm=%.3f)", acc_norm);
            if (_last_orientation.valid) {
                orientation = _last_orientation;
                return ESP_OK;
            } else {
                orientation.valid = false;
                return ESP_FAIL;
            }
        }
    }

    // anglenormalize
    normalize_angles(orientation);
    orientation.valid = true;
    
    // BMI270 implementation note.
    _last_orientation = orientation;
    
    ESP_LOGD(TAG, "Orientation: Roll=%.2f°, Pitch=%.2f°, Yaw=%.2f° (stationary=%s, beta=%.3f, motion=%.1f°/s)", 
             orientation.x_rotation, orientation.y_rotation, orientation.z_rotation, 
             is_stationary ? "YES" : "NO", _madgwick_filter.beta, gyro_norm_deg);
    
    return ESP_OK;
}

// BMI270 implementation note.

esp_err_t bmi270_tools::get_quaternion_madgwick(quaternion_data_t &quat_data, bool use_magnetometer)
{
    if (!_initialized) {
        ESP_LOGE(TAG, "BMI270 not initialized");
        quat_data.valid = false;
        return ESP_FAIL;
    }

    // readsensordata
    sensor_data_t sensor_data;
    esp_err_t rslt = get_sensor_data(sensor_data);
    
    if (rslt != ESP_OK || !sensor_data.acc_valid) {
        ESP_LOGE(TAG, "Failed to read sensor data for Madgwick filter");
        quat_data.valid = false;
        return ESP_FAIL;
    }

    // initializationMadgwickfilter
    if (!_madgwick_filter.initialized) {
        _madgwick_filter.beta = 0.5f;          // default gain
        _madgwick_filter.q0 = 1.0f;            // initial quaternion
        _madgwick_filter.q1 = 0.0f;
        _madgwick_filter.q2 = 0.0f;
        _madgwick_filter.q3 = 0.0f;
        _madgwick_filter.last_update_ms = esp_timer_get_time() / 1000;
        _madgwick_filter.initialized = true;
        ESP_LOGI(TAG, "Madgwick filter initialized");
    }

    // BMI270 implementation note.
    uint32_t current_time_ms = esp_timer_get_time() / 1000;
    float dt = (current_time_ms - _madgwick_filter.last_update_ms) / 1000.0f;
    _madgwick_filter.last_update_ms = current_time_ms;

    // BMI270 implementation note.
    if (dt > 0.1f) dt = 0.01f;  // BMI270 implementation note.
    if (dt < 0.001f) dt = 0.001f;  // BMI270 implementation note.

    // applysensorcalibration
    sensor_data_t calibrated_data = sensor_data;
    apply_sensor_calibration(calibrated_data);

    // convertdataunit
    float gx = calibrated_data.gyr_x * M_PI / 180.0f;  // BMI270 implementation note.
    float gy = calibrated_data.gyr_y * M_PI / 180.0f;
    float gz = calibrated_data.gyr_z * M_PI / 180.0f;
    
    float ax = calibrated_data.acc_x / GRAVITY_EARTH;   // BMI270 implementation note.
    float ay = calibrated_data.acc_y / GRAVITY_EARTH;
    float az = calibrated_data.acc_z / GRAVITY_EARTH;

    // BMI270 implementation note.
    if (use_magnetometer && is_magnetometer_enabled() && calibrated_data.mag_valid) {
        // BMI270 implementation note.
        float mx = (float)calibrated_data.mag_x;
        float my = (float)calibrated_data.mag_y;
        float mz = (float)calibrated_data.mag_z;
        
        madgwick_update_9dof(gx, gy, gz, ax, ay, az, mx, my, mz, dt);
        ESP_LOGD(TAG, "Using 9-DOF Madgwick fusion");
    } else {
        // BMI270 implementation note.
        madgwick_update_6dof(gx, gy, gz, ax, ay, az, dt);
        ESP_LOGD(TAG, "Using 6-DOF Madgwick fusion");
    }

    // outputquaternion
    quat_data.w = _madgwick_filter.q0;
    quat_data.x = _madgwick_filter.q1;
    quat_data.y = _madgwick_filter.q2;
    quat_data.z = _madgwick_filter.q3;
    quat_data.valid = true;

    ESP_LOGD(TAG, "Madgwick quaternion: w=%.4f, x=%.4f, y=%.4f, z=%.4f", 
             quat_data.w, quat_data.x, quat_data.y, quat_data.z);

    return ESP_OK;
}

void bmi270_tools::madgwick_update_6dof(float gx, float gy, float gz, float ax, float ay, float az, float dt)
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2, _8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

    // getcurrentquaternion
    float q0 = _madgwick_filter.q0;
    float q1 = _madgwick_filter.q1;
    float q2 = _madgwick_filter.q2;
    float q3 = _madgwick_filter.q3;

    // BMI270 implementation note.
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // BMI270 implementation note.
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        
        // normalizeaccelerometerdata
        recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // auxiliary variables
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _4q0 = 4.0f * q0;
        _4q1 = 4.0f * q1;
        _4q2 = 4.0f * q2;
        _8q1 = 8.0f * q1;
        _8q2 = 8.0f * q2;
        q0q0 = q0 * q0;
        q1q1 = q1 * q1;
        q2q2 = q2 * q2;
        q3q3 = q3 * q3;

        // gradient descent,objective function
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
        
        // normalizestep size
        recipNorm = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // applyfeedback
        qDot1 -= _madgwick_filter.beta * s0;
        qDot2 -= _madgwick_filter.beta * s1;
        qDot3 -= _madgwick_filter.beta * s2;
        qDot4 -= _madgwick_filter.beta * s3;
    }

    // integratequaternionderivative
    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // normalizequaternion
    recipNorm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    _madgwick_filter.q0 = q0 * recipNorm;
    _madgwick_filter.q1 = q1 * recipNorm;
    _madgwick_filter.q2 = q2 * recipNorm;
    _madgwick_filter.q3 = q3 * recipNorm;
}

void bmi270_tools::madgwick_update_9dof(float gx, float gy, float gz, float ax, float ay, float az, 
                                        float mx, float my, float mz, float dt)
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float hx, hy;
    float _2q0mx, _2q0my, _2q0mz, _2q1mx, _2bx, _2bz, _4bx, _4bz, _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3, q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;

    // getcurrentquaternion
    float q0 = _madgwick_filter.q0;
    float q1 = _madgwick_filter.q1;
    float q2 = _madgwick_filter.q2;
    float q3 = _madgwick_filter.q3;

    // BMI270 implementation note.
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // BMI270 implementation note.
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f)) && !((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f))) {
        
        // normalizeaccelerometer
        recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm;
        ay *= recipNorm;
        az *= recipNorm;

        // normalizemagnetometer
        recipNorm = inv_sqrt(mx * mx + my * my + mz * mz);
        mx *= recipNorm;
        my *= recipNorm;
        mz *= recipNorm;

        // auxiliary variables
        _2q0mx = 2.0f * q0 * mx;
        _2q0my = 2.0f * q0 * my;
        _2q0mz = 2.0f * q0 * mz;
        _2q1mx = 2.0f * q1 * mx;
        _2q0 = 2.0f * q0;
        _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2;
        _2q3 = 2.0f * q3;
        _2q0q2 = 2.0f * q0 * q2;
        _2q2q3 = 2.0f * q2 * q3;
        q0q0 = q0 * q0;
        q0q1 = q0 * q1;
        q0q2 = q0 * q2;
        q0q3 = q0 * q3;
        q1q1 = q1 * q1;
        q1q2 = q1 * q2;
        q1q3 = q1 * q3;
        q2q2 = q2 * q2;
        q2q3 = q2 * q3;
        q3q3 = q3 * q3;

        // reference direction
        hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
        hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
        _2bx = sqrtf(hx * hx + hy * hy);
        _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
        _4bx = 2.0f * _2bx;
        _4bz = 2.0f * _2bz;

        // gradient descent
        s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q1 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + _2bz * q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q2 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        
        // normalize
        recipNorm = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recipNorm;
        s1 *= recipNorm;
        s2 *= recipNorm;
        s3 *= recipNorm;

        // applyfeedback
        qDot1 -= _madgwick_filter.beta * s0;
        qDot2 -= _madgwick_filter.beta * s1;
        qDot3 -= _madgwick_filter.beta * s2;
        qDot4 -= _madgwick_filter.beta * s3;
    }

    // integratequaternionderivative
    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // normalizequaternion
    recipNorm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    _madgwick_filter.q0 = q0 * recipNorm;
    _madgwick_filter.q1 = q1 * recipNorm;
    _madgwick_filter.q2 = q2 * recipNorm;
    _madgwick_filter.q3 = q3 * recipNorm;
}

float bmi270_tools::inv_sqrt(float x)
{
    // BMI270 implementation note.
    float halfx = 0.5f * x;
    
    union {
        float f;
        long i;
    } conv;
    
    conv.f = x;
    conv.i = 0x5f3759df - (conv.i >> 1);
    float y = conv.f;
    y = y * (1.5f - (halfx * y * y));
    return y;
}

void bmi270_tools::set_madgwick_beta(float beta)
{
    if (beta >= 0.1f && beta <= 2.0f) {
        _madgwick_filter.beta = beta;
        ESP_LOGI(TAG, "Madgwick filter beta set to %.3f", beta);
    } else {
        ESP_LOGW(TAG, "Invalid beta value %.3f, valid range is 0.1-2.0", beta);
    }
}

void bmi270_tools::reset_attitude_filter()
{
    _madgwick_filter.q0 = 1.0f;
    _madgwick_filter.q1 = 0.0f;
    _madgwick_filter.q2 = 0.0f;
    _madgwick_filter.q3 = 0.0f;
    _madgwick_filter.last_update_ms = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Attitude filter reset");
}

// === sensor calibration implementation ===

esp_err_t bmi270_tools::calibrate_sensors(uint32_t calibration_time_ms, uint32_t sample_count)
{
    ESP_LOGI(TAG, "Starting sensor calibration (time: %ldms, samples: %ld)", calibration_time_ms, sample_count);
    
    esp_err_t result = ESP_OK;
    
    // calibrationgyroscope
    if (calibrate_gyroscope(sample_count) != ESP_OK) {
        ESP_LOGW(TAG, "Gyroscope calibration failed");
        result = ESP_FAIL;
    }
    
    // calibrationaccelerometer
    if (calibrate_accelerometer(sample_count) != ESP_OK) {
        ESP_LOGW(TAG, "Accelerometer calibration failed");
        result = ESP_FAIL;
    }
    
    // calibrationmagnetometer (ifenable)
    if (is_magnetometer_enabled()) {
        if (calibrate_magnetometer(sample_count) != ESP_OK) {
            ESP_LOGW(TAG, "Magnetometer calibration failed");
            result = ESP_FAIL;
        }
    }
    
    if (result == ESP_OK) {
        _calibration.calibrated = true;
        ESP_LOGI(TAG, "Sensor calibration completed successfully");
    } else {
        ESP_LOGW(TAG, "Sensor calibration completed with errors");
    }
    
    return result;
}

esp_err_t bmi270_tools::calibrate_gyroscope(uint32_t sample_count)
{
    ESP_LOGI(TAG, "Calibrating gyroscope... Keep device stationary");
    
    float sum_x = 0, sum_y = 0, sum_z = 0;
    uint32_t valid_samples = 0;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        sensor_data_t data;
        if (get_sensor_data(data) == ESP_OK && data.gyr_valid) {
            sum_x += data.gyr_x;
            sum_y += data.gyr_y;
            sum_z += data.gyr_z;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // BMI270 implementation note.
    }
    
    if (valid_samples < sample_count / 2) {
        ESP_LOGE(TAG, "Insufficient gyroscope samples for calibration");
        return ESP_FAIL;
    }
    
    _calibration.gyro_offset[0] = sum_x / valid_samples;
    _calibration.gyro_offset[1] = sum_y / valid_samples;
    _calibration.gyro_offset[2] = sum_z / valid_samples;
    
    ESP_LOGI(TAG, "Gyroscope bias: X=%.3f, Y=%.3f, Z=%.3f deg/s", 
             _calibration.gyro_offset[0], _calibration.gyro_offset[1], _calibration.gyro_offset[2]);
    
    return ESP_OK;
}

esp_err_t bmi270_tools::calibrate_accelerometer(uint32_t sample_count)
{
    ESP_LOGI(TAG, "Calibrating accelerometer... Keep device flat on level surface");
    
    float sum_x = 0, sum_y = 0, sum_z = 0;
    uint32_t valid_samples = 0;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        sensor_data_t data;
        if (get_sensor_data(data) == ESP_OK && data.acc_valid) {
            sum_x += data.acc_x;
            sum_y += data.acc_y;
            sum_z += data.acc_z;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // BMI270 implementation note.
    }
    
    if (valid_samples < sample_count / 2) {
        ESP_LOGE(TAG, "Insufficient accelerometer samples for calibration");
        return ESP_FAIL;
    }
    
    _calibration.acc_offset[0] = sum_x / valid_samples;
    _calibration.acc_offset[1] = sum_y / valid_samples;
    _calibration.acc_offset[2] = sum_z / valid_samples - GRAVITY_EARTH;  // BMI270 implementation note.
    
    // BMI270 implementation note.
    _calibration.acc_scale[0] = 1.0f;
    _calibration.acc_scale[1] = 1.0f;
    _calibration.acc_scale[2] = 1.0f;
    
    ESP_LOGI(TAG, "Accelerometer bias: X=%.3f, Y=%.3f, Z=%.3f m/s²", 
             _calibration.acc_offset[0], _calibration.acc_offset[1], _calibration.acc_offset[2]);
    
    return ESP_OK;
}

esp_err_t bmi270_tools::calibrate_magnetometer(uint32_t sample_count)
{
    ESP_LOGI(TAG, "Calibrating magnetometer... Rotate device in figure-8 pattern");
    
    float min_x = FLT_MAX, max_x = -FLT_MAX;
    float min_y = FLT_MAX, max_y = -FLT_MAX;
    float min_z = FLT_MAX, max_z = -FLT_MAX;
    uint32_t valid_samples = 0;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        sensor_data_t data;
        if (get_sensor_data(data) == ESP_OK && data.mag_valid) {
            float mx = (float)data.mag_x;
            float my = (float)data.mag_y;
            float mz = (float)data.mag_z;
            
            if (mx < min_x) min_x = mx;
            if (mx > max_x) max_x = mx;
            if (my < min_y) min_y = my;
            if (my > max_y) max_y = my;
            if (mz < min_z) min_z = mz;
            if (mz > max_z) max_z = mz;
            
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // BMI270 implementation note.
    }
    
    if (valid_samples < sample_count / 2) {
        ESP_LOGE(TAG, "Insufficient magnetometer samples for calibration");
        return ESP_FAIL;
    }
    
    // hard-iron offsetcalibration
    _calibration.mag_offset[0] = (max_x + min_x) / 2.0f;
    _calibration.mag_offset[1] = (max_y + min_y) / 2.0f;
    _calibration.mag_offset[2] = (max_z + min_z) / 2.0f;
    
    // BMI270 implementation note.
    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    float range_z = max_z - min_z;
    float avg_range = (range_x + range_y + range_z) / 3.0f;
    
    // BMI270 implementation note.
    memset(_calibration.mag_scale, 0, sizeof(_calibration.mag_scale));
    _calibration.mag_scale[0][0] = avg_range / range_x;
    _calibration.mag_scale[1][1] = avg_range / range_y;
    _calibration.mag_scale[2][2] = avg_range / range_z;
    
    ESP_LOGI(TAG, "Magnetometer hard iron offset: X=%.1f, Y=%.1f, Z=%.1f uT", 
             _calibration.mag_offset[0], _calibration.mag_offset[1], _calibration.mag_offset[2]);
    ESP_LOGI(TAG, "Magnetometer scale factors: X=%.3f, Y=%.3f, Z=%.3f", 
             _calibration.mag_scale[0][0], _calibration.mag_scale[1][1], _calibration.mag_scale[2][2]);
    
    return ESP_OK;
}

void bmi270_tools::apply_sensor_calibration(sensor_data_t &data)
{
    if (!_calibration.calibrated) return;
    
    // applygyroscopeoffsetcalibration
    if (data.gyr_valid) {
        data.gyr_x -= _calibration.gyro_offset[0];
        data.gyr_y -= _calibration.gyro_offset[1];
        data.gyr_z -= _calibration.gyro_offset[2];
    }
    
    // BMI270 implementation note.
    if (data.acc_valid) {
        data.acc_x = (data.acc_x - _calibration.acc_offset[0]) * _calibration.acc_scale[0];
        data.acc_y = (data.acc_y - _calibration.acc_offset[1]) * _calibration.acc_scale[1];
        data.acc_z = (data.acc_z - _calibration.acc_offset[2]) * _calibration.acc_scale[2];
    }
    
    // BMI270 implementation note.
    if (data.mag_valid) {
        float mx = (float)data.mag_x - _calibration.mag_offset[0];
        float my = (float)data.mag_y - _calibration.mag_offset[1];
        float mz = (float)data.mag_z - _calibration.mag_offset[2];
        
        // applysoft-iron correction matrix
        float mx_cal = _calibration.mag_scale[0][0] * mx + _calibration.mag_scale[0][1] * my + _calibration.mag_scale[0][2] * mz;
        float my_cal = _calibration.mag_scale[1][0] * mx + _calibration.mag_scale[1][1] * my + _calibration.mag_scale[1][2] * mz;
        float mz_cal = _calibration.mag_scale[2][0] * mx + _calibration.mag_scale[2][1] * my + _calibration.mag_scale[2][2] * mz;
        
        data.mag_x = (int16_t)mx_cal;
        data.mag_y = (int16_t)my_cal;
        data.mag_z = (int16_t)mz_cal;
    }
}

// BMI270 implementation note.

void bmi270_tools::apply_low_pass_filter_3d(low_pass_filter_t &filter, float &x, float &y, float &z)
{
    if (!filter.initialized) {
        filter.x = x;
        filter.y = y;
        filter.z = z;
        filter.alpha = 0.8f;  // BMI270 implementation note.
        filter.initialized = true;
    } else {
        filter.x = filter.alpha * x + (1.0f - filter.alpha) * filter.x;
        filter.y = filter.alpha * y + (1.0f - filter.alpha) * filter.y;
        filter.z = filter.alpha * z + (1.0f - filter.alpha) * filter.z;
        x = filter.x;
        y = filter.y;
        z = filter.z;
    }
}

void bmi270_tools::apply_low_pass_filter_1d(float &current, float new_value, float alpha)
{
    current = alpha * new_value + (1.0f - alpha) * current;
}

bool bmi270_tools::validate_sensor_data(const sensor_data_t &data)
{
    // BMI270 implementation note.
    if (data.acc_valid) {
        float acc_norm = sqrtf(data.acc_x * data.acc_x + data.acc_y * data.acc_y + data.acc_z * data.acc_z);
        // BMI270 implementation note.
        if (acc_norm < 2.0f || acc_norm > 30.0f) {
            ESP_LOGW(TAG, "Accelerometer data out of range: norm=%.2f m/s²", acc_norm);
            return false;
        }
    }
    
    // BMI270 implementation note.
    if (data.gyr_valid) {
        float gyro_norm = sqrtf(data.gyr_x * data.gyr_x + data.gyr_y * data.gyr_y + data.gyr_z * data.gyr_z);
        // BMI270 implementation note.
        if (gyro_norm > 2000.0f) {
            ESP_LOGW(TAG, "Gyroscope data out of range: norm=%.2f °/s", gyro_norm);
            return false;
        }
    }
    
    // BMI270 implementation note.
    if (data.mag_valid) {
        float mag_norm = sqrtf((float)data.mag_x * data.mag_x + (float)data.mag_y * data.mag_y + (float)data.mag_z * data.mag_z);
        // BMI270 implementation note.
        if (mag_norm < 10.0f || mag_norm > 200.0f) {
            ESP_LOGD(TAG, "Magnetometer data may be interfered: norm=%.1f uT", mag_norm);
            // BMI270 implementation note.
        }
    }
    
    return true;
}

float bmi270_tools::calculate_adaptive_gain(float acc_norm, float gyro_norm, float mag_norm)
{
    // BMI270 implementation note.
    float base_gain = 0.2f;
    
    // BMI270 implementation note.
    float acc_confidence = 1.0f;
    if (acc_norm > 0.1f) {
        float acc_error = fabsf(acc_norm - 1.0f);
        acc_confidence = expf(-acc_error * 2.0f);  // BMI270 implementation note.
    }
    
    // BMI270 implementation note.
    float gyro_confidence = 1.0f;
    if (gyro_norm > 0.1f) {
        // BMI270 implementation note.
        gyro_confidence = expf(-gyro_norm / 50.0f);  // BMI270 implementation note.
    }
    
    // BMI270 implementation note.
    float mag_confidence = 1.0f;
    if (mag_norm > 0.1f) {
        // BMI270 implementation note.
        float mag_error = fabsf(mag_norm - 50.0f) / 50.0f;  // assumeEarth magnetic field50uT
        mag_confidence = expf(-mag_error * 1.0f);
    }
    
    // BMI270 implementation note.
    float total_confidence = acc_confidence * gyro_confidence * mag_confidence;
    
    // BMI270 implementation note.
    float adaptive_gain = base_gain + (0.6f - base_gain) * total_confidence;
    
    // limitgainrange
    if (adaptive_gain < 0.05f) adaptive_gain = 0.05f;
    if (adaptive_gain > 0.8f) adaptive_gain = 0.8f;
    
    return adaptive_gain;
}

void bmi270_tools::normalize_angles(absolute_orientation_t &orientation)
{
    // BMI270 implementation note.
    while (orientation.x_rotation > 180.0f) orientation.x_rotation -= 360.0f;
    while (orientation.x_rotation < -180.0f) orientation.x_rotation += 360.0f;
    
    while (orientation.y_rotation > 180.0f) orientation.y_rotation -= 360.0f;
    while (orientation.y_rotation < -180.0f) orientation.y_rotation += 360.0f;
    
    while (orientation.z_rotation > 180.0f) orientation.z_rotation -= 360.0f;
    while (orientation.z_rotation < -180.0f) orientation.z_rotation += 360.0f;
}

float bmi270_tools::angle_difference(float angle1, float angle2)
{
    // BMI270 implementation note.
    float diff = angle1 - angle2;
    
    // BMI270 implementation note.
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    
    return diff;
}

// BMI270 implementation note.

void bmi270_tools::update_motion_detector(const sensor_data_t &data)
{
    // BMI270 implementation note.
    if (_motion_detector.sample_count == 0) {
        _motion_detector.acc_threshold = 0.3f;     // BMI270 implementation note.
        _motion_detector.gyro_threshold = 5.0f;    // BMI270 implementation note.
        _motion_detector.stable_time_ms = 2000;    // BMI270 implementation note.
        _motion_detector.last_motion_time = esp_timer_get_time() / 1000;
        _motion_detector.is_stationary = false;
        
        // initializationaccumulated value
        for (int i = 0; i < 3; i++) {
            _motion_detector.acc_sum[i] = 0.0f;
            _motion_detector.acc_sum_sq[i] = 0.0f;
            _motion_detector.gyro_sum[i] = 0.0f;
            _motion_detector.gyro_sum_sq[i] = 0.0f;
            _motion_detector.acc_variance[i] = 0.0f;
            _motion_detector.gyro_variance[i] = 0.0f;
        }
    }

    // BMI270 implementation note.
    const uint32_t window_size = 50;  // BMI270 implementation note.
    
    // cumulativecurrentsample
    _motion_detector.acc_sum[0] += data.acc_x;
    _motion_detector.acc_sum[1] += data.acc_y;
    _motion_detector.acc_sum[2] += data.acc_z;
    _motion_detector.acc_sum_sq[0] += data.acc_x * data.acc_x;
    _motion_detector.acc_sum_sq[1] += data.acc_y * data.acc_y;
    _motion_detector.acc_sum_sq[2] += data.acc_z * data.acc_z;
    
    _motion_detector.gyro_sum[0] += data.gyr_x;
    _motion_detector.gyro_sum[1] += data.gyr_y;
    _motion_detector.gyro_sum[2] += data.gyr_z;
    _motion_detector.gyro_sum_sq[0] += data.gyr_x * data.gyr_x;
    _motion_detector.gyro_sum_sq[1] += data.gyr_y * data.gyr_y;
    _motion_detector.gyro_sum_sq[2] += data.gyr_z * data.gyr_z;
    
    _motion_detector.sample_count++;
    
    // BMI270 implementation note.
    if (_motion_detector.sample_count >= window_size) {
        for (int i = 0; i < 3; i++) {
            float acc_mean = _motion_detector.acc_sum[i] / window_size;
            float acc_mean_sq = _motion_detector.acc_sum_sq[i] / window_size;
            _motion_detector.acc_variance[i] = acc_mean_sq - acc_mean * acc_mean;
            
            float gyro_mean = _motion_detector.gyro_sum[i] / window_size;
            float gyro_mean_sq = _motion_detector.gyro_sum_sq[i] / window_size;
            _motion_detector.gyro_variance[i] = gyro_mean_sq - gyro_mean * gyro_mean;
        }
        
        // BMI270 implementation note.
        float acc_total_variance = _motion_detector.acc_variance[0] + 
                                  _motion_detector.acc_variance[1] + 
                                  _motion_detector.acc_variance[2];
        float gyro_total_variance = _motion_detector.gyro_variance[0] + 
                                   _motion_detector.gyro_variance[1] + 
                                   _motion_detector.gyro_variance[2];
        
        bool motion_detected = (sqrtf(acc_total_variance) > _motion_detector.acc_threshold) ||
                              (sqrtf(gyro_total_variance) > _motion_detector.gyro_threshold);
        
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        if (motion_detected) {
            _motion_detector.last_motion_time = current_time;
            _motion_detector.is_stationary = false;
        } else {
            // BMI270 implementation note.
            if ((current_time - _motion_detector.last_motion_time) > _motion_detector.stable_time_ms) {
                _motion_detector.is_stationary = true;
            }
        }
        
        // BMI270 implementation note.
        if (_motion_detector.sample_count > window_size * 1.5f) {
            float factor = 0.5f;  // BMI270 implementation note.
            for (int i = 0; i < 3; i++) {
                _motion_detector.acc_sum[i] *= factor;
                _motion_detector.acc_sum_sq[i] *= factor;
                _motion_detector.gyro_sum[i] *= factor;
                _motion_detector.gyro_sum_sq[i] *= factor;
            }
            _motion_detector.sample_count = window_size / 2;
        }
    }
}

bool bmi270_tools::is_device_stationary()
{
    return _motion_detector.is_stationary;
}

void bmi270_tools::update_gyro_bias_calibration(const sensor_data_t &data)
{
    // BMI270 implementation note.
    if (!is_device_stationary()) {
        return;
    }
    
    // initializationcalibration
    if (!_gyro_bias.calibrated && _gyro_bias.calibration_samples == 0) {
        _gyro_bias.calibration_sum[0] = 0.0f;
        _gyro_bias.calibration_sum[1] = 0.0f;
        _gyro_bias.calibration_sum[2] = 0.0f;
        ESP_LOGI(TAG, "Starting gyroscope bias calibration...");
    }
    
    // cumulativesample
    _gyro_bias.calibration_sum[0] += data.gyr_x;
    _gyro_bias.calibration_sum[1] += data.gyr_y;
    _gyro_bias.calibration_sum[2] += data.gyr_z;
    _gyro_bias.calibration_samples++;
    
    // BMI270 implementation note.
    const uint32_t required_samples = 200;  // BMI270 implementation note.
    if (_gyro_bias.calibration_samples >= required_samples) {
        _gyro_bias.bias[0] = _gyro_bias.calibration_sum[0] / required_samples;
        _gyro_bias.bias[1] = _gyro_bias.calibration_sum[1] / required_samples;
        _gyro_bias.bias[2] = _gyro_bias.calibration_sum[2] / required_samples;
        _gyro_bias.calibrated = true;
        
        ESP_LOGI(TAG, "Gyroscope bias calibration completed: X=%.3f°/s, Y=%.3f°/s, Z=%.3f°/s",
                 _gyro_bias.bias[0], _gyro_bias.bias[1], _gyro_bias.bias[2]);
        
        // BMI270 implementation note.
        _gyro_bias.calibration_samples = 0;
        _gyro_bias.calibration_sum[0] = 0.0f;
        _gyro_bias.calibration_sum[1] = 0.0f;
        _gyro_bias.calibration_sum[2] = 0.0f;
    }
}

void bmi270_tools::apply_gyro_bias_correction(sensor_data_t &data)
{
    if (_gyro_bias.calibrated) {
        data.gyr_x -= _gyro_bias.bias[0];
        data.gyr_y -= _gyro_bias.bias[1];
        data.gyr_z -= _gyro_bias.bias[2];
    }
}

void bmi270_tools::reset_orientation_drift()
{
    // BMI270 implementation note.
    sensor_data_t current_data;
    if (get_sensor_data(current_data) == ESP_OK && current_data.acc_valid) {
        float ax = current_data.acc_x / GRAVITY_EARTH;
        float ay = current_data.acc_y / GRAVITY_EARTH;
        float az = current_data.acc_z / GRAVITY_EARTH;
        float acc_norm = sqrtf(ax*ax + ay*ay + az*az);
        
        if (acc_norm > 0.8f && acc_norm < 1.2f) {  // ensurenear1g
            // normalize
            ax /= acc_norm; ay /= acc_norm; az /= acc_norm;
            
            // BMI270 implementation note.
            float target_roll = atan2f(ay, az);
            float target_pitch = atan2f(-ax, sqrtf(ay*ay + az*az));
            
            // BMI270 implementation note.
            float current_yaw = atan2f(2.0f * (_madgwick_filter.q1 * _madgwick_filter.q2 + _madgwick_filter.q0 * _madgwick_filter.q3),
                                      _madgwick_filter.q0 * _madgwick_filter.q0 + _madgwick_filter.q1 * _madgwick_filter.q1 - 
                                      _madgwick_filter.q2 * _madgwick_filter.q2 - _madgwick_filter.q3 * _madgwick_filter.q3);
            
            // BMI270 implementation note.
            float cr = cosf(target_roll * 0.5f);
            float sr = sinf(target_roll * 0.5f);
            float cp = cosf(target_pitch * 0.5f);
            float sp = sinf(target_pitch * 0.5f);
            float cy = cosf(current_yaw * 0.5f);
            float sy = sinf(current_yaw * 0.5f);
            
            _madgwick_filter.q0 = cr * cp * cy + sr * sp * sy;
            _madgwick_filter.q1 = sr * cp * cy - cr * sp * sy;
            _madgwick_filter.q2 = cr * sp * cy + sr * cp * sy;
            _madgwick_filter.q3 = cr * cp * sy - sr * sp * cy;
            
            // normalizequaternion
            float norm = sqrtf(_madgwick_filter.q0 * _madgwick_filter.q0 + 
                              _madgwick_filter.q1 * _madgwick_filter.q1 + 
                              _madgwick_filter.q2 * _madgwick_filter.q2 + 
                              _madgwick_filter.q3 * _madgwick_filter.q3);
            _madgwick_filter.q0 /= norm;
            _madgwick_filter.q1 /= norm;
            _madgwick_filter.q2 /= norm;
            _madgwick_filter.q3 /= norm;
            
            ESP_LOGI(TAG, "Orientation drift reset based on gravity vector");
        }
    }
}

void bmi270_tools::ensure_angle_continuity(absolute_orientation_t &current, const absolute_orientation_t &previous)
{
    if (!previous.valid) return;
    
    // BMI270 implementation note.
    auto fix_continuity_robust = [](float &current_angle, float previous_angle) {
        // BMI270 implementation note.
        float diff = current_angle - previous_angle;
        
        // BMI270 implementation note.
        while (diff > 180.0f) {
            diff -= 360.0f;
        }
        while (diff < -180.0f) {
            diff += 360.0f;
        }
        
        // BMI270 implementation note.
        if (fabsf(diff) > 90.0f) {
            ESP_LOGW(TAG, "Large angle discontinuity detected: %.1f° -> %.1f° (diff=%.1f°)", 
                     previous_angle, current_angle, diff);
            
            // BMI270 implementation note.
            float candidates[3] = {
                current_angle,
                current_angle + 360.0f,
                current_angle - 360.0f
            };
            
            float min_diff = fabsf(diff);
            float best_angle = current_angle;
            
            for (int i = 1; i < 3; i++) {
                float test_diff = fabsf(candidates[i] - previous_angle);
                if (test_diff < min_diff) {
                    min_diff = test_diff;
                    best_angle = candidates[i];
                }
            }
            
            if (best_angle != current_angle) {
                ESP_LOGI(TAG, "Corrected angle from %.1f° to %.1f°", current_angle, best_angle);
                current_angle = best_angle;
            }
        } else {
            // BMI270 implementation note.
            current_angle = previous_angle + diff;
        }
    };
    
    fix_continuity_robust(current.x_rotation, previous.x_rotation);
    fix_continuity_robust(current.y_rotation, previous.y_rotation);
    fix_continuity_robust(current.z_rotation, previous.z_rotation);
}

void bmi270_tools::apply_quaternion_filter(float &q0, float &q1, float &q2, float &q3, bool is_stationary)
{
    // initializationquaternionfilter
    if (!_quat_filter.initialized) {
        _quat_filter.q0 = q0;
        _quat_filter.q1 = q1;
        _quat_filter.q2 = q2;
        _quat_filter.q3 = q3;
        _quat_filter.alpha = 0.8f;  // BMI270 implementation note.
        _quat_filter.initialized = true;
        return;
    }
    
    // BMI270 implementation note.
    float alpha = is_stationary ? 0.95f : 0.7f;  // BMI270 implementation note.
    
    // BMI270 implementation note.
    float dot = _quat_filter.q0 * q0 + _quat_filter.q1 * q1 + _quat_filter.q2 * q2 + _quat_filter.q3 * q3;
    if (dot < 0.0f) {
        // BMI270 implementation note.
        q0 = -q0; q1 = -q1; q2 = -q2; q3 = -q3;
    }
    
    // BMI270 implementation note.
    _quat_filter.q0 = alpha * q0 + (1.0f - alpha) * _quat_filter.q0;
    _quat_filter.q1 = alpha * q1 + (1.0f - alpha) * _quat_filter.q1;
    _quat_filter.q2 = alpha * q2 + (1.0f - alpha) * _quat_filter.q2;
    _quat_filter.q3 = alpha * q3 + (1.0f - alpha) * _quat_filter.q3;
    
    // BMI270 implementation note.
    float norm = sqrtf(_quat_filter.q0 * _quat_filter.q0 + _quat_filter.q1 * _quat_filter.q1 + 
                      _quat_filter.q2 * _quat_filter.q2 + _quat_filter.q3 * _quat_filter.q3);
    if (norm > 1e-6f) {
        _quat_filter.q0 /= norm;
        _quat_filter.q1 /= norm;
        _quat_filter.q2 /= norm;
        _quat_filter.q3 /= norm;
    }
    
    // BMI270 implementation note.
    q0 = _quat_filter.q0;
    q1 = _quat_filter.q1;
    q2 = _quat_filter.q2;
    q3 = _quat_filter.q3;
}

float bmi270_tools::unwrap_angle(float angle, int axis)
{
    // initializationunwrapper
    if (!_angle_unwrapper.initialized) {
        for (int i = 0; i < 3; i++) {
            _angle_unwrapper.cumulative_offset[i] = 0.0f;
            _angle_unwrapper.last_wrapped_angle[i] = 0.0f;
            _angle_unwrapper.last_unwrapped_angle[i] = 0.0f;
        }
        _angle_unwrapper.initialized = true;
        _angle_unwrapper.last_wrapped_angle[axis] = angle;
        _angle_unwrapper.last_unwrapped_angle[axis] = angle;
        return angle;
    }
    
    // BMI270 implementation note.
    float diff = angle - _angle_unwrapper.last_wrapped_angle[axis];
    
    // BMI270 implementation note.
    if (diff > 180.0f) {
        // BMI270 implementation note.
        _angle_unwrapper.cumulative_offset[axis] -= 360.0f;
    } else if (diff < -180.0f) {
        // BMI270 implementation note.
        _angle_unwrapper.cumulative_offset[axis] += 360.0f;
    }
    
    // calculateunwrappedangle
    float unwrapped = angle + _angle_unwrapper.cumulative_offset[axis];
    
    // BMI270 implementation note.
    _angle_unwrapper.last_wrapped_angle[axis] = angle;
    _angle_unwrapper.last_unwrapped_angle[axis] = unwrapped;
    
    return unwrapped;
}

bmi270_tools::absolute_orientation_t bmi270_tools::quaternion_to_xyz_rotation_stable(const quaternion_data_t &quat)
{
    absolute_orientation_t xyz_rotation;
    
    if (!quat.valid) {
        xyz_rotation.x_rotation = xyz_rotation.y_rotation = xyz_rotation.z_rotation = 0.0f;
        xyz_rotation.valid = false;
        return xyz_rotation;
    }
    
    float x = quat.x, y = quat.y, z = quat.z, w = quat.w;
    
    // BMI270 implementation note.
    // BMI270 implementation note.
    
    // Roll (Xaxisrotation)
    float sinr_cosp = 2.0f * (w * x + y * z);
    float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
    float roll = atan2f(sinr_cosp, cosr_cosp) * 180.0f / (float)M_PI;
    
    // Pitch (Yaxisrotation)
    float sinp = 2.0f * (w * y - z * x);
    float pitch;
    if (fabsf(sinp) >= 1.0f) {
        pitch = copysignf(90.0f, sinp); // BMI270 implementation note.
    } else {
        pitch = asinf(sinp) * 180.0f / (float)M_PI;
    }
    
    // Yaw (Zaxisrotation)
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    float yaw = atan2f(siny_cosp, cosy_cosp) * 180.0f / (float)M_PI;
    
    // BMI270 implementation note.
    if (_angle_tracker.initialized) {
        // BMI270 implementation note.
        roll = smart_angle_unwrap(roll, _angle_tracker.last_roll);
        pitch = smart_angle_unwrap(pitch, _angle_tracker.last_pitch);
        yaw = smart_angle_unwrap(yaw, _angle_tracker.last_yaw);
        
        // BMI270 implementation note.
        float roll_rate = (roll - _angle_tracker.last_roll) / 0.01f; // BMI270 implementation note.
        float pitch_rate = (pitch - _angle_tracker.last_pitch) / 0.01f;
        float yaw_rate = (yaw - _angle_tracker.last_yaw) / 0.01f;
        
        // BMI270 implementation note.
        const float max_rate = 1000.0f;
        if (fabsf(roll_rate) > max_rate) {
            ESP_LOGW(TAG, "Roll rate too high: %.1f°/s, using prediction", roll_rate);
            roll = _angle_tracker.last_roll + _angle_tracker.roll_rate * 0.01f;
        } else {
            // BMI270 implementation note.
            _angle_tracker.roll_rate = 0.8f * _angle_tracker.roll_rate + 0.2f * roll_rate;
        }
        
        if (fabsf(pitch_rate) > max_rate) {
            ESP_LOGW(TAG, "Pitch rate too high: %.1f°/s, using prediction", pitch_rate);
            pitch = _angle_tracker.last_pitch + _angle_tracker.pitch_rate * 0.01f;
        } else {
            _angle_tracker.pitch_rate = 0.8f * _angle_tracker.pitch_rate + 0.2f * pitch_rate;
        }
        
        if (fabsf(yaw_rate) > max_rate) {
            ESP_LOGW(TAG, "Yaw rate too high: %.1f°/s, using prediction", yaw_rate);
            yaw = _angle_tracker.last_yaw + _angle_tracker.yaw_rate * 0.01f;
        } else {
            _angle_tracker.yaw_rate = 0.8f * _angle_tracker.yaw_rate + 0.2f * yaw_rate;
        }
    } else {
        // BMI270 implementation note.
        _angle_tracker.initialized = true;
        _angle_tracker.roll_rate = 0.0f;
        _angle_tracker.pitch_rate = 0.0f;
        _angle_tracker.yaw_rate = 0.0f;
    }
    
    // BMI270 implementation note.
    _angle_tracker.last_roll = roll;
    _angle_tracker.last_pitch = pitch;
    _angle_tracker.last_yaw = yaw;
    
    xyz_rotation.x_rotation = roll;
    xyz_rotation.y_rotation = pitch;
    xyz_rotation.z_rotation = yaw;
    xyz_rotation.valid = true;
    
    return xyz_rotation;
}

void bmi270_tools::reset_angle_unwrapper()
{
    _angle_unwrapper.initialized = false;
    for (int i = 0; i < 3; i++) {
        _angle_unwrapper.cumulative_offset[i] = 0.0f;
        _angle_unwrapper.last_wrapped_angle[i] = 0.0f;
        _angle_unwrapper.last_unwrapped_angle[i] = 0.0f;
    }
    ESP_LOGI(TAG, "Angle unwrapper reset");
}

void bmi270_tools::reset_angle_tracking()
{
    reset_angle_unwrapper();
    _last_orientation.valid = false;
    _quat_filter.initialized = false;
    // BMI270 implementation note.
    _angle_tracker.initialized = false;
    ESP_LOGI(TAG, "Angle tracking system reset");
}

// add the new smart angle unwrapping function
float bmi270_tools::smart_angle_unwrap(float current_angle, float previous_angle)
{
    // calculateshortest-path difference
    float diff = current_angle - previous_angle;
    
    // BMI270 implementation note.
    if (diff > 180.0f) {
        // BMI270 implementation note.
        current_angle -= 360.0f;
    } else if (diff < -180.0f) {
        // BMI270 implementation note.
        current_angle += 360.0f;
    }
    
    // BMI270 implementation note.
    diff = current_angle - previous_angle;
    if (fabsf(diff) > 90.0f) {
        // BMI270 implementation note.
        ESP_LOGD(TAG, "Large angle change detected: %.1f° -> %.1f° (diff=%.1f°)", 
                 previous_angle, current_angle, diff);
    }
    
    return current_angle;
}

// add the smooth angle transition function
float bmi270_tools::smooth_angle_transition(float new_angle, float old_angle, float factor)
{
    // calculateshortest-path difference
    float diff = new_angle - old_angle;
    
    // BMI270 implementation note.
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    
    // BMI270 implementation note.
    float smoothed = old_angle + diff * (1.0f - factor);
    
    // BMI270 implementation note.
    while (smoothed > 180.0f) smoothed -= 360.0f;
    while (smoothed < -180.0f) smoothed += 360.0f;
    
    return smoothed;
}
