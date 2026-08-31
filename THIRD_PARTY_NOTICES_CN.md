# 第三方声明

[English](THIRD_PARTY_NOTICES.md) | [简体中文](THIRD_PARTY_NOTICES_CN.md)

本项目包含或依赖下列第三方软件。项目的 MIT 许可证不会取代适用于这些组件的许可证。随仓库内置源码和 Git 子模块提供的许可证文本具有最终效力。

| 组件 | 集成方式 | 版本或提交 | 来源 | 许可证 | 仓库内许可证依据 |
|---|---|---|---|---|---|
| M5GFX | Git 子模块 | `d91077b9a607b59404e4e4a49f775c792bfae382` (`0.2.28`) | [m5stack/M5GFX](https://github.com/m5stack/M5GFX) | MIT | `components/M5GFX/LICENSE` |
| M5PM1 | Git 子模块 | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` (`1.0.7`) | [m5stack/M5PM1](https://github.com/m5stack/M5PM1) | MIT | `components/M5PM1/LICENSE` |
| M5IOE1 | Git 子模块 | `846eec7d05e25c09013be2acdb8804487f48a62e` (`1.0.9`) | [m5stack/M5IOE1](https://github.com/m5stack/M5IOE1) | MIT | `components/M5IOE1/LICENSE` |
| ic_rx8130 | Git 子模块 | `d79bf99aa25168ab26886d0e94cf525be5a1343e` | [Ocean-lhy/ic_rx8130](https://github.com/Ocean-lhy/ic_rx8130) | MIT（源文件中的 SPDX 标识） | `components/ic_rx8130/rx8130.cpp`、`components/ic_rx8130/rx8130.h` |
| Espressif I2C Bus | 内置组件快照 | `1.5.2`，上游提交 `ef13dcfd5aa18c4d6dca3d89d18173f8ae180a5f` | [esp-iot-solution/components/i2c_bus](https://github.com/espressif/esp-iot-solution/tree/master/components/i2c_bus) | Apache-2.0 | `components/espressif__i2c_bus/license.txt` |
| Espressif CMake Utilities | 内置组件快照 | `1.1.1`，上游提交 `05f51bd7044e9f95bc978093fc78656e4d81cd37` | [esp-iot-solution/tools/cmake_utilities](https://github.com/espressif/esp-iot-solution/tree/master/tools/cmake_utilities) | Apache-2.0 | `components/espressif__cmake_utilities/license.txt` |
| otool_bmi270 封装 | 内置源码快照 | 项目本地快照；未确认上游仓库 | `components/otool_bmi270/` | MIT（源文件中的 SPDX 标识） | `components/otool_bmi270/bmi270_tools.cpp`、`components/otool_bmi270/bmi270_tools.h` |
| Bosch BMI270 Sensor API | 内置于 `otool_bmi270` | 未记录快照版本 | 随本地封装快照分发 | BSD-3-Clause | `components/otool_bmi270/BMI270_SensorAPI/LICENSE` |
| Bosch BMM150 Sensor API | 内置于 `otool_bmi270` | 未记录快照版本 | 随本地封装快照分发 | BSD-3-Clause | `components/otool_bmi270/BMM150_SensorAPI/LICENSE` |
| espressif/esp_codec_dev | 内置组件快照 | `1.6.2`，ADF 提交 `af7b72fb2b73d4f513d3cede01c13518ab216735` | [esp-adf/components/esp_codec_dev](https://github.com/espressif/esp-adf/tree/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev) | Apache-2.0 | `components/espressif__esp_codec_dev/LICENSE` |

## 已知元数据缺口

- `ic_rx8130` 快照的源文件包含 MIT SPDX 标识，但根目录没有独立许可证文件。
- `otool_bmi270` 封装作为项目本地源码快照维护，因为无法确认其上游仓库。
