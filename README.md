# M5StopWatch Power Demo

## Overview

This repository contains independent ESP-IDF firmware projects for measuring the power consumption of the production M5StopWatch hardware. Each test is built and flashed separately, starts automatically after boot, and uses shared board support modules.

The project targets ESP-IDF 5.5.5 and provides independent firmware projects for each power test scenario.

> **Full Load safety gate:** Do not flash or run `tests/full_load` until the fixture rating, current limit, external temperature limit, and manual stop procedure have been reviewed. Its local safety acknowledgement is disabled by default.

## Test Matrix

| Test item | Peripherals enabled or tested | Code directory | `WINDOW Avg` current | Reference screenshot |
|---|---|---|---|---|
| L0 shutdown | M5PM1, BATTERY, PM1 shutdown path | `tests/l0_shutdown/` | 3.7 V: 8.89 uA<br>4.2 V: 9.08 uA | [3.7 V](docs/power-graphs/l0_shutdown/3.7v.png), [4.2 V](docs/power-graphs/l0_shutdown/4.2v.png) |
| L1 standby | M5PM1, RTC, IMU, BATTERY | `tests/l1_standby/` | 3.7 V: 20.49 uA<br>4.2 V: 20.67 uA | [3.7 V](docs/power-graphs/l1_standby/3.7v.png), [4.2 V](docs/power-graphs/l1_standby/4.2v.png) |
| L2 deep sleep | M5PM1, M5IOE1, IMU suspend path, BATTERY, ESP32-S3 Deep Sleep; no wake source | `tests/l2_deepsleep/` | 3.7 V: 90.06 uA<br>4.2 V: 51.65 uA | [3.7 V](docs/power-graphs/l2_deep_sleep/3.7v.png), [4.2 V](docs/power-graphs/l2_deep_sleep/4.2v.png) |
| IMU wake L1 reference | M5PM1, IMU, RTC, BATTERY; Accel, Gyro, Wrist-wear and Any-motion | `tests/imu_wake_l1/` (`M5SW_IMU_WAKE_L1_REFERENCE_PROFILE=1`) | 3.7 V: 670.94 uA<br>4.2 V: 671.55 uA | [3.7 V](docs/power-graphs/imu_wake_l1/3.7v.png), [4.2 V](docs/power-graphs/imu_wake_l1/4.2v.png) |
| IMU wake L1 low power | M5PM1, IMU, RTC, BATTERY; Accel and Any-motion | `tests/imu_wake_l1/` (`M5SW_IMU_WAKE_L1_REFERENCE_PROFILE=0`) | 3.7 V: 37.56 uA<br>4.2 V: 39.17 uA | [3.7 V](docs/power-graphs/imu_wake_l1/3.7v-low.png), [4.2 V](docs/power-graphs/imu_wake_l1/4.2v-low.png) |
| IMU wake L2 reference | M5PM1, M5IOE1, RTC status, IMU, BATTERY, ESP32-S3 Deep Sleep; Accel, Gyro, Wrist-wear and Any-motion | `tests/imu_wake_l2/` (`M5SW_IMU_WAKE_L2_REFERENCE_PROFILE=1`) | 3.7 V: 708.35 uA<br>4.2 V: 694.76 uA | [3.7 V](docs/power-graphs/imu_wake_l2/3.7v.png), [4.2 V](docs/power-graphs/imu_wake_l2/4.2v.png) |
| IMU wake L2 low power | M5PM1, M5IOE1, RTC status, IMU, BATTERY, ESP32-S3 Deep Sleep; Accel and Any-motion | `tests/imu_wake_l2/` (`M5SW_IMU_WAKE_L2_REFERENCE_PROFILE=0`) | 3.7 V: 73.91 uA<br>4.2 V: 60.50 uA | [3.7 V](docs/power-graphs/imu_wake_l2/3.7v-low.png), [4.2 V](docs/power-graphs/imu_wake_l2/4.2v-low.png) |
| RTC alarm wake L1 | M5PM1, RTC, IMU, BATTERY; IMU status checked for false-trigger rejection | `tests/rtc_alarm_wake_l1/` | 3.7 V: 20.44 uA<br>4.2 V: 20.62 uA | [3.7 V](docs/power-graphs/rtc_alarm_wake_l1/3.7v.png), [4.2 V](docs/power-graphs/rtc_alarm_wake_l1/4.2v.png) |
| RTC alarm wake L2 | M5PM1, M5IOE1, RTC, IMU, BATTERY, ESP32-S3 Deep Sleep; IMU status checked for false-trigger rejection | `tests/rtc_alarm_wake_l2/` | 3.7 V: 57.06 uA<br>4.2 V: 43.44 uA | [3.7 V](docs/power-graphs/rtc_alarm_wake_l2/3.7v.png), [4.2 V](docs/power-graphs/rtc_alarm_wake_l2/4.2v.png) |
| RTC timer wake L1 | M5PM1, RTC, IMU, BATTERY; IMU status checked for false-trigger rejection | `tests/rtc_time_wake_l1/` | 3.7 V: 20.46 uA<br>4.2 V: 20.52 uA | [3.7 V](docs/power-graphs/rtc_time_wake_l1/3.7v.png), [4.2 V](docs/power-graphs/rtc_time_wake_l1/4.2v.png) |
| RTC timer wake L2 | M5PM1, M5IOE1, RTC, IMU, BATTERY, ESP32-S3 Deep Sleep; IMU status checked for false-trigger rejection | `tests/rtc_time_wake_l2/` | 3.7 V: 630.98 uA<br>4.2 V: 622.45 uA | [3.7 V](docs/power-graphs/rtc_time_wake_l2/3.7v.png), [4.2 V](docs/power-graphs/rtc_time_wake_l2/4.2v.png) |
| Normal active | M5PM1, M5IOE1, AMOLED, BUTTON, TP, IMU, RTC, BATTERY | `tests/normal_active/` | 3.7 V: 45.69 mA<br>4.2 V: 44.53 mA | [3.7 V](docs/power-graphs/normal_active/3.7v.png), [4.2 V](docs/power-graphs/normal_active/4.2v.png) |
| Display only | M5PM1, M5IOE1, AMOLED, BATTERY | `tests/display_only/` | 3.7 V: 154.45 mA<br>4.2 V: 144.24 mA | [3.7 V](docs/power-graphs/display_only/3.7v.png), [4.2 V](docs/power-graphs/display_only/4.2v.png) |
| IMU sampling | M5PM1, M5IOE1, IMU, BATTERY | `tests/imu_sampling/` | 3.7 V: 36.32 mA<br>4.2 V: 35.61 mA | [3.7 V](docs/power-graphs/imu_sampling/3.7v.png), [4.2 V](docs/power-graphs/imu_sampling/4.2v.png) |
| Wi-Fi connected | M5PM1, M5IOE1, ESP32-S3/SOC Wi-Fi, BATTERY; L2-active baseline with RTC retained and AMOLED/BUTTON/TP/IMU/AUDIO/SPK/MOTOR/Ext.Port 5 V output disabled; connected with `WIFI_PS_NONE` and no application traffic | `tests/wifi_connected/` | 3.7 V: 105.73 mA<br>4.2 V: 98.67 mA | [3.7 V](docs/power-graphs/wifi_connected/3.7v.png), [4.2 V](docs/power-graphs/wifi_connected/4.2v.png) |
| Wi-Fi Light Sleep | M5PM1, M5IOE1, ESP32-S3/SOC Wi-Fi, BATTERY; L2-active baseline with RTC retained and AMOLED/BUTTON/TP/IMU/AUDIO/SPK/MOTOR/Ext.Port 5 V output disabled; automatic Light Sleep with `WIFI_PS_MAX_MODEM` | `tests/wifi_light_sleep/` | 3.7 V: 3.49 mA<br>4.2 V: 3.43 mA | [3.7 V](docs/power-graphs/wifi_light_sleep/3.7v.png), [4.2 V](docs/power-graphs/wifi_light_sleep/4.2v.png) |
| Wi-Fi periodic traffic | M5PM1, M5IOE1, ESP32-S3/SOC Wi-Fi, BATTERY; L2-active baseline with RTC retained and AMOLED/BUTTON/TP/IMU/AUDIO/SPK/MOTOR/Ext.Port 5 V output disabled; `WIFI_PS_NONE` with one 64-byte ICMP Echo every second | `tests/wifi_periodic_traffic/` | 3.7 V: 105.09 mA<br>4.2 V: 97.83 mA | [3.7 V](docs/power-graphs/wifi_periodic_traffic/3.7v.png), [4.2 V](docs/power-graphs/wifi_periodic_traffic/4.2v.png) |
| Full load | M5PM1, M5IOE1, AMOLED, TP, ESP32-S3/SOC Wi-Fi initialization, AUDIO, SPK, MOTOR, BATTERY | `tests/full_load/` | 3.7 V: 285.77 mA<br>4.2 V: 276.32 mA | [3.7 V](docs/power-graphs/full_load/3.7v.png), [4.2 V](docs/power-graphs/full_load/4.2v.png) |

L0 does not enable or test RTC functionality. The RX8130 backup domain remains physically connected and is part of the measured whole-board current.

The Wi-Fi tests use the same controlled 2.4 GHz access point. Record the actual
channel, RSSI, beacon interval and DTIM for each run; `wifi_light_sleep` uses a
STA listen interval of three beacons.

## Repository Layout

```text
components/  Third-party libraries and fixed source snapshots
modules/     Shared M5StopWatch board and test support components
tests/       Independent ESP-IDF firmware projects
```

## Build and Flash

Install and activate ESP-IDF 5.5.5, then initialize the repository dependencies:

```bash
git submodule update --init --recursive
```

Build and flash one test from its own directory:

```bash
cd tests/l0_shutdown
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Each test directory contains its own `sdkconfig.defaults`. The required third-party ESP-IDF components are included or pinned in this repository, so a generated `dependencies.lock` file is not required for a standard build.

Each firmware reports a canonical log identifier: `test_id` and `config_id` both
match the test directory name, with underscores converted to hyphens and no
version suffix (for example, `imu-wake-l2`).

`tests/imu_wake_l1` and `tests/imu_wake_l2` each provide two profiles in the
same firmware source. Select the profile before building by changing the
corresponding `M5SW_IMU_WAKE_L1_REFERENCE_PROFILE` or
`M5SW_IMU_WAKE_L2_REFERENCE_PROFILE` macro near the top of `main/main.cpp`:

```bash
# 0: optimized 50 Hz Accel + Any-motion profile
# 1: full reference Accel + Gyro + Wrist-wear + Any-motion profile
idf.py build
```

The firmware reports `profile_id=imu-wake-l1` or `profile_id=imu-wake-l2` in
`BOARD_OK`. The accompanying `PROFILE=low_power` or `PROFILE=reference` field
distinguishes measurements while both profiles keep the same project identifier,
sdkconfig, and build command.

## Measurement Method

1. Connect Type-C to flash the firmware and confirm `TEST_START`, `BOARD_OK`, and `TEST_READY` through the serial monitor.
2. Keep the same firmware and configuration, disconnect Type-C, and power the device only through the measurement setup.
3. Measure at 3.7 V and 4.2 V using an approximately 60-second `WINDOW`. Publish the selected capture's `WINDOW Avg`. When multiple captures are available for one voltage, select the representative capture whose `WINDOW Avg` is the median reading.

All matrix values are taken directly from the linked selected graph. Standard filenames identify the reference or only profile; the `-low` suffix identifies the optimized IMU wake `low_power` profile.

## Representative Graphs

### L0 Shutdown

![L0 shutdown, 3.7 V](docs/power-graphs/l0_shutdown/3.7v.png)
![L0 shutdown, 4.2 V](docs/power-graphs/l0_shutdown/4.2v.png)

### L1 Standby

![L1 standby, 3.7 V](docs/power-graphs/l1_standby/3.7v.png)
![L1 standby, 4.2 V](docs/power-graphs/l1_standby/4.2v.png)

### L2 Deep Sleep

![L2 deep sleep, 3.7 V](docs/power-graphs/l2_deep_sleep/3.7v.png)
![L2 deep sleep, 4.2 V](docs/power-graphs/l2_deep_sleep/4.2v.png)

### IMU Wake L1 Low Power

![IMU wake L1 low power, 3.7 V](docs/power-graphs/imu_wake_l1/3.7v-low.png)
![IMU wake L1 low power, 4.2 V](docs/power-graphs/imu_wake_l1/4.2v-low.png)

### IMU Wake L2 Low Power

![IMU wake L2 low power, 3.7 V](docs/power-graphs/imu_wake_l2/3.7v-low.png)
![IMU wake L2 low power, 4.2 V](docs/power-graphs/imu_wake_l2/4.2v-low.png)

The firmware does not detect the power source or switch between validation and measurement modes.

## Dependencies

- [M5GFX](https://github.com/m5stack/M5GFX) at `d91077b9a607b59404e4e4a49f775c792bfae382`
- [M5PM1](https://github.com/m5stack/M5PM1) at `be9a5456c007c333e7ac963f33bfde1ffa5d82ee`
- [M5IOE1](https://github.com/m5stack/M5IOE1) at `846eec7d05e25c09013be2acdb8804487f48a62e`
- [ic_rx8130](https://github.com/Ocean-lhy/ic_rx8130) at `d79bf99aa25168ab26886d0e94cf525be5a1343e`
- `otool_bmi270` (source snapshot included in `components/otool_bmi270`)
- `esp_codec_dev` 1.6.2 (vendored in `components/espressif__esp_codec_dev`)

Dependency revisions and licenses are summarized
in [Third-Party Notices](THIRD_PARTY_NOTICES.md).

## License

Project-owned source code is released under the [MIT License](LICENSE).
Third-party components remain subject to the licenses described in
[Third-Party Notices](THIRD_PARTY_NOTICES.md).
