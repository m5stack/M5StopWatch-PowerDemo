# IMU Wake L2 Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware validates wake-up from ESP32-S3 Deep Sleep triggered by BMI270 any-motion detection through the PM1 GPIO0 and ESP32 `ext0` paths. It also measures whole-board input current while the device waits in Deep Sleep.

## Enabled Modules

- M5PM1 power manager and GPIO0 wake route
- M5IOE1 low-power state configuration
- RX8130 RTC status checks for ruling out non-target wake events
- BMI270 IMU any-motion detection engine
- ESP32-S3 deep sleep and `ext0` wake
- Shared wake and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; DCDC and LDO enabled; watchdog timeout `0`; GPIO0 falling-edge wake; GPIO1 aggregate IRQ output; I2C idle-sleep time `1 s` |
| M5IOE1 | I2C address `0x4F`, 100 kHz; external loads disabled; PWM stopped; idle-sleep time `1 s` |
| BMI270 | I2C address `0x68`, 100 kHz, Base API; any-motion detection threshold `30`, duration `10`; INT1 active-high, push-pull and non-latched |
| Full-feature mode | Accelerometer +/-2 g at 100 Hz, gyroscope +/-2000 dps at 100 Hz; accelerometer, gyroscope, wrist-wear wake-up and any-motion detection enabled |
| Low-power mode | Accelerometer +/-2 g at 50 Hz in power-optimized AVG1 mode; accelerometer and any-motion detection enabled; gyroscope and wrist-wear wake-up disabled |
| RX8130 | Interrupt sources disabled; retained flags checked to rule out non-target RTC wake events |
| ESP32-S3 wake | `ext0` on GPIO12, active-low |
| Wake path | BMI270 INT1 -> PM1 GPIO0 -> PM1 GPIO1 -> ESP32 GPIO12 |
| Preparation | 3-second application delay, followed by the final 1-second L2 settling interval |

## Runtime Mode

The firmware reads and clears retained status flags, switches the peripherals to their specified L2 low-power states, configures the selected IMU mode, arms the PM1 GPIO0 and ESP32 GPIO12 wake chain, reports readiness, waits three seconds, and enters Deep Sleep. After wake, it verifies the ESP wake cause, PM1 GPIO IRQ status and BMI270 any-motion status before clearing the recorded status flags and reporting the result.

## Operating Modes

Select `M5SW_IMU_WAKE_L2_REFERENCE_PROFILE` in `main/main.cpp`:

- `1`, `reference`: accelerometer, gyroscope, wrist-wear wake-up and any-motion detection enabled.
- `0`, `low_power`: accelerometer at the 50 Hz rate required by any-motion detection in power-optimized AVG1 mode; only the accelerometer and any-motion detection are enabled, while the gyroscope and wrist-wear wake-up are disabled.

Both modes use `test_id=imu-wake-l2` and `config_id=imu-wake-l2`.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

| Mode | 3.7 V | 4.2 V |
|---|---:|---:|
| Full-feature mode | 708.35 uA | 694.76 uA |
| Low-power mode | 73.91 uA | 60.50 uA |

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

Expected logs include `BOARD_OK`, `WAKE_ARMED`, `TEST_READY`, `WAKE_CAPTURE`, `WAKE_CLEAR_OK` and `TEST_PASS`. Disconnect USB Type-C before power measurement.
