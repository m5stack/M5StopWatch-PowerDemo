idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        freertos
        i2c_bus
        log
        m5sw_board
        m5sw_power
        m5sw_test_support
        otool_bmi270
)
