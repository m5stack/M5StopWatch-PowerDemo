# Third-Party Notices

[English](THIRD_PARTY_NOTICES.md) | [简体中文](THIRD_PARTY_NOTICES_CN.md)

This project includes or depends on the third-party software listed below. The
project's MIT License does not replace the licenses that apply to these
components. License texts included with vendored source and submodules remain
authoritative.

| Component | Integration | Version or commit | Source | License | License evidence in repository |
|---|---|---|---|---|---|
| M5GFX | Git submodule | `d91077b9a607b59404e4e4a49f775c792bfae382` (`0.2.28`) | [m5stack/M5GFX](https://github.com/m5stack/M5GFX) | MIT | `components/M5GFX/LICENSE` |
| M5PM1 | Git submodule | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` (`1.0.7`) | [m5stack/M5PM1](https://github.com/m5stack/M5PM1) | MIT | `components/M5PM1/LICENSE` |
| M5IOE1 | Git submodule | `846eec7d05e25c09013be2acdb8804487f48a62e` (`1.0.9`) | [m5stack/M5IOE1](https://github.com/m5stack/M5IOE1) | MIT | `components/M5IOE1/LICENSE` |
| ic_rx8130 | Git submodule | `d79bf99aa25168ab26886d0e94cf525be5a1343e` | [Ocean-lhy/ic_rx8130](https://github.com/Ocean-lhy/ic_rx8130) | MIT (SPDX source headers) | `components/ic_rx8130/rx8130.cpp`, `components/ic_rx8130/rx8130.h` |
| Espressif I2C Bus | Vendored component snapshot | `1.5.2`, upstream commit `ef13dcfd5aa18c4d6dca3d89d18173f8ae180a5f` | [esp-iot-solution/components/i2c_bus](https://github.com/espressif/esp-iot-solution/tree/master/components/i2c_bus) | Apache-2.0 | `components/espressif__i2c_bus/license.txt` |
| Espressif CMake Utilities | Vendored component snapshot | `1.1.1`, upstream commit `05f51bd7044e9f95bc978093fc78656e4d81cd37` | [esp-iot-solution/tools/cmake_utilities](https://github.com/espressif/esp-iot-solution/tree/master/tools/cmake_utilities) | Apache-2.0 | `components/espressif__cmake_utilities/license.txt` |
| otool_bmi270 wrapper | Vendored source snapshot | Project-local snapshot; upstream repository not identified | `components/otool_bmi270/` | MIT (SPDX identifiers in source files) | `components/otool_bmi270/bmi270_tools.cpp`, `components/otool_bmi270/bmi270_tools.h` |
| Bosch BMI270 Sensor API | Vendored under `otool_bmi270` | Snapshot version not recorded | Distributed with the local wrapper snapshot | BSD-3-Clause | `components/otool_bmi270/BMI270_SensorAPI/LICENSE` |
| Bosch BMM150 Sensor API | Vendored under `otool_bmi270` | Snapshot version not recorded | Distributed with the local wrapper snapshot | BSD-3-Clause | `components/otool_bmi270/BMM150_SensorAPI/LICENSE` |
| espressif/esp_codec_dev | Vendored component snapshot | `1.6.2`, ADF commit `af7b72fb2b73d4f513d3cede01c13518ab216735` | [esp-adf/components/esp_codec_dev](https://github.com/espressif/esp-adf/tree/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev) | Apache-2.0 | `components/espressif__esp_codec_dev/LICENSE` |

## Known Metadata Gaps

- The `ic_rx8130` snapshot has MIT SPDX identifiers in its source files but no
  standalone license file at its root.
- The `otool_bmi270` wrapper is maintained as a project-local source snapshot
  because its upstream repository could not be identified.
