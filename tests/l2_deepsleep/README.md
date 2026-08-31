# L2 Deep Sleep Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current in the M5StopWatch L2 deep-sleep state with no wake source enabled. It also verifies that external peripherals enter their specified low-power states before ESP32-S3 Deep Sleep.

## Enabled Modules

- M5PM1 power manager
- M5IOE1 peripheral controller
- ESP32-S3 deep sleep
- Shared M5StopWatch board and test support

RTC and IMU wake sources are disabled for this test. Display, touch, Wi-Fi, audio and motor loads are disabled.

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; DCDC and LDO enabled; charger, boost and status LED disabled; LDO hold enabled; watchdog timeout `0`; I2C idle-sleep time `1 s` |
| M5IOE1 | I2C address `0x4F`, 100 kHz; factory reset with load-control outputs left in a safe high-impedance state; PWM stopped; idle-sleep time `1 s` |
| ESP32-S3 | Deep Sleep with all wake sources disabled |
| Wake sources | None; PM1 aggregate IRQ output is not enabled |
| Preparation | 3-second application delay, followed by the final 1-second L2 settling interval |

## Runtime Mode

The firmware initializes PM1, prepares L2 with `WakeSelection::none`, disables external loads, configures safe high-impedance states and I2C idle sleep, reports readiness, waits three seconds, and enters ESP32-S3 deep sleep without a wake source.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 90.06 uA
- 4.2 V: 51.65 uA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

| Setting | Value |
|---|---|
| Target | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | OPI, 80 MHz |
| CPU | 240 MHz |
| I2C | 100 kHz |
| Test ID | `l2-deepsleep` |
| Config ID | `l2-deepsleep` |

## Build and Flash

Use ESP-IDF 5.5.5:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Use serial monitoring only for functional validation. Disconnect USB Type-C and stop monitoring before power measurement.

## Expected Validation

Confirm `TEST_START`, `BOARD_OK PM1=1 IOE1=1 WAKE=none` and `TEST_READY`. The firmware should enter deep sleep without further peripheral transactions.
