idf_component_register(
    SRCS
        "m5sw_rtc_wakeup.cpp"
        "m5sw_imu_wakeup.cpp"
        "m5sw_wakeup.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ic_rx8130 i2c_bus otool_bmi270 m5sw_power esp_driver_gpio log freertos
)
