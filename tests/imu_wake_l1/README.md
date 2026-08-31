# IMU Wake L1 Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware validates wake-up triggered by BMI270 any-motion detection through the PM1 GPIO0 path and measures whole-board input current while the board waits in L1 standby.

## Enabled Modules

- M5PM1 power manager
- RX8130 RTC for non-target wake rejection and status clearing
- BMI270 IMU any-motion detection engine
- PM1 GPIO0 wake path
- Shared wake and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; LDO hold enabled; GPIO0 configured as pull-up input with falling-edge wake; GPIO4 wake disabled |
| BMI270 | I2C address `0x68`, 100 kHz, Base API; any-motion detection threshold `30`, duration `10`; INT1 active-high, push-pull and non-latched |
| Full-feature mode | Accelerometer +/-2 g at 100 Hz, gyroscope +/-2000 dps at 100 Hz; accelerometer, gyroscope, wrist-wear wake-up and any-motion detection enabled |
| Low-power mode | Accelerometer +/-2 g at 50 Hz in power-optimized AVG1 mode; accelerometer and any-motion detection enabled; gyroscope and wrist-wear wake-up disabled |
| RX8130 | I2C address `0x32`, 100 kHz; alarm, timer and update interrupts disabled to rule out non-target RTC wake events |
| Wake path | BMI270 INT1 -> PM1 GPIO0 -> PM1 L1 wake |
| Preparation | 3-second delay after `TEST_READY` before L1 entry |

## Runtime Mode

The firmware reads the retained RTC and IMU status flags, clears residual events, disables RTC interrupts, configures BMI270 INT1 and PM1 GPIO0, reports readiness, waits three seconds, and enters L1 standby. After a motion wake, it verifies the PM1 and BMI270 status, rules out a non-target RTC wake event, clears the recorded status flags and reports the result.

## Operating Modes

Select the mode with `M5SW_IMU_WAKE_L1_REFERENCE_PROFILE` near the top of `main/main.cpp`:

- `1`, `reference`: accelerometer, gyroscope, wrist-wear wake-up and any-motion detection enabled.
- `0`, `low_power`: accelerometer at the 50 Hz rate required by any-motion detection in power-optimized AVG1 mode; only the accelerometer and any-motion detection are enabled, while the gyroscope and wrist-wear wake-up are disabled.

Both modes keep `test_id=imu-wake-l1` and `config_id=imu-wake-l1`.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

| Mode | 3.7 V | 4.2 V |
|---|---:|---:|
| Full-feature mode | 670.94 uA | 671.55 uA |
| Low-power mode | 37.56 uA | 39.17 uA |

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU and 100 kHz I2C. Use ESP-IDF 5.5.5.

## Build and Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Confirm `TEST_START`, `BOARD_OK`, `WAKE_ARMED`, `TEST_READY`, `WAKE_CAPTURE`, `WAKE_CLEAR_OK` and `TEST_PASS`. Disconnect USB Type-C before power measurement.
