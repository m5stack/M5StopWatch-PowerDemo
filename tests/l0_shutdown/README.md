# L0 Shutdown Test

## Purpose

This firmware measures the M5StopWatch L0 shutdown state. It verifies the PM1 shutdown command and the battery-powered static current path.

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

The firmware initializes the shared I2C bus, starts PM1, reports board readiness, waits three seconds, and enters L0 shutdown. `TEST_READY` is the last expected serial message before the PM1 shutdown path removes the console power path.

## Power

The reported current is the `WINDOW Avg` from the selected representative capture.

- 3.7 V: 8.89 uA
- 4.2 V: 9.08 uA

![L0 shutdown, 3.7 V](../../docs/power-graphs/l0_shutdown/3.7v.png)
![L0 shutdown, 4.2 V](../../docs/power-graphs/l0_shutdown/4.2v.png)

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

Use serial monitoring only for Stage A functional validation. For Stage B power measurement, disconnect Type-C and do not run the serial monitor. Generated `build/`, `sdkconfig` and `dependencies.lock` files are local artifacts.

## Expected Validation

Confirm `TEST_START`, `BOARD_OK PM1=1` and `TEST_READY`. After shutdown, host-side serial loss is expected and is not itself a firmware failure.
