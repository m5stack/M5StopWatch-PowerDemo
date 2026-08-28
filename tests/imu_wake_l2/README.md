# IMU Wake L2 Test

## Purpose

This firmware validates BMI270 Any-motion wake from ESP32-S3 deep sleep through the PM1 GPIO0 and ESP32 `ext0` wake paths.

## Enabled Modules

- M5PM1 power manager and GPIO0 wake route
- M5IOE1 low-power preparation
- RX8130 RTC status capture and false-trigger rejection
- BMI270 IMU Any-motion engine
- ESP32-S3 deep sleep and `ext0` wake
- Shared wake and test support

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; DCDC and LDO enabled; watchdog timeout `0`; GPIO0 falling-edge wake; GPIO1 aggregate IRQ output; I2C idle-sleep time `1 s` |
| M5IOE1 | I2C address `0x4F`, 100 kHz; external loads disabled; PWM stopped; idle-sleep time `1 s` |
| BMI270 | I2C address `0x68`, 100 kHz, Base API; Any-motion threshold `30`, duration `10`; INT1 active-high, push-pull and non-latched |
| Reference profile | Accelerometer +/-2 g at 100 Hz, gyroscope +/-2000 dps at 100 Hz; Accel, Gyro, Wrist-wear and Any-motion enabled |
| Low-power profile | Accelerometer +/-2 g at 50 Hz in power-optimized AVG1 mode; Accel and Any-motion enabled; gyroscope and wrist-wear disabled |
| RX8130 | Interrupt sources disabled and retained flags used for false-trigger rejection |
| ESP32-S3 wake | `ext0` on GPIO12, active-low |
| Wake path | BMI270 INT1 -> PM1 GPIO0 -> PM1 GPIO1 -> ESP32 GPIO12 |
| Preparation | 3-second application delay, followed by the final 1-second L2 settling interval |

## Runtime Mode

The firmware captures and clears retained status, prepares the complete L2 power state, configures the selected IMU profile, arms the PM1 GPIO0 and ESP32 GPIO12 wake chain, reports readiness, waits three seconds, and enters deep sleep. After wake it validates ESP wake cause, PM1 GPIO IRQ evidence and BMI270 Any-motion evidence before clearing the snapshot and reporting the result.

## Profiles

Select `M5SW_IMU_WAKE_L2_REFERENCE_PROFILE` in `main/main.cpp`:

- `1`, `reference`: accelerometer, gyroscope, wrist-wear and Any-motion features.
- `0`, `low_power`: accelerometer at the Any-motion feature rate of 50 Hz in power-optimized AVG1 mode; only Accel and Any-motion are enabled, while gyroscope and wrist-wear are disabled.

Both profiles use `test_id=imu-wake-l2` and `config_id=imu-wake-l2`.

## Power

The reported current is the `WINDOW Avg` from the selected capture for each profile.

| Profile | 3.7 V | 4.2 V |
|---|---:|---:|
| Reference | 708.35 uA | 694.76 uA |
| Low power | 73.91 uA | 60.50 uA |

![IMU wake L2 reference, 3.7 V](../../docs/power-graphs/imu_wake_l2/3.7v.png)
![IMU wake L2 reference, 4.2 V](../../docs/power-graphs/imu_wake_l2/4.2v.png)
![IMU wake L2 low power, 3.7 V](../../docs/power-graphs/imu_wake_l2/3.7v-low.png)
![IMU wake L2 low power, 4.2 V](../../docs/power-graphs/imu_wake_l2/4.2v-low.png)

## Configuration

ESP32-S3, 16 MB Flash, OPI PSRAM at 80 MHz, 240 MHz CPU and 100 kHz I2C. Use ESP-IDF 5.5.5:

## Build and Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Expected logs include `BOARD_OK`, `WAKE_ARMED`, `TEST_READY`, `WAKE_CAPTURE`, `WAKE_CLEAR_OK` and `TEST_PASS`. Disconnect Type-C before Stage B power measurement.
