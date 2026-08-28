idf_component_register(
    SRCS "m5sw_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES M5GFX m5sw_board m5sw_power esp_driver_gpio esp_timer freertos
)
