# L0 Shutdown Test

[English](README.md) | [简体中文](README_CN.md)

## Purpose

This firmware measures whole-board input current in the M5StopWatch L0 shutdown state. It also verifies the PM1 shutdown command and the quiescent-current path under battery-equivalent power.

## Enabled Modules

- M5PM1 power manager
- Battery power path
- Shared M5StopWatch board support
- Test logging and status support

RTC, BMI270, M5IOE1, display, touch, Wi-Fi, audio and motor functions are intentionally not initialized.

## Module Configuration

| Module | Configuration |
|---|---|
| M5PM1 | I2C address `0x6E`, 100 kHz; charger and boost disabled; LDO hold disabled; GPIO0 and GPIO4 wake disabled; PM1 shutdown command used for L0 entry |
| Wake sources | None |
| Preparation | 3-second delay after `TEST_READY` before L0 entry |
| Disabled loads | M5IOE1, RTC, IMU, display, touch, Wi-Fi, audio, motor and external 5 V output |

## Runtime Mode

The firmware initializes the shared I2C bus and PM1 driver, reports board readiness, waits three seconds, and enters L0 shutdown. `TEST_READY` is the last expected serial message before PM1 cuts power to the ESP32-S3.

## Measured Current

The current shown in the measurement results is the `WINDOW Avg` from the measurement record.

- 3.7 V: 8.89 uA
- 4.2 V: 9.08 uA

[View the measurement graphs](../../docs/power-graphs/README.md).

## Configuration

| Setting | Value |
|---|---|
| Target | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | OPI, 80 MHz |
| CPU | 240 MHz |
| I2C | 100 kHz |
| Test ID | `l0-shutdown` |
| Config ID | `l0-shutdown` |

## Build and Flash

Use ESP-IDF 5.5.5 from this directory:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Use serial monitoring only for functional validation. For power measurement, disconnect USB Type-C and do not run the serial monitor. Generated `build/`, `sdkconfig` and `dependencies.lock` files are local artifacts.

## Expected Validation

Confirm `TEST_START`, `BOARD_OK PM1=1` and `TEST_READY`. After shutdown, host-side serial loss is expected and is not itself a firmware failure.
