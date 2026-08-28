idf_component_register(
    SRCS "m5sw_audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        espressif__esp_codec_dev
        esp_driver_i2s
        freertos
        i2c_bus
        m5sw_board
        m5sw_power
)
