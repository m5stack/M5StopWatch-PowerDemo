idf_component_register(
    SRCS "m5sw_test_support.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_app_format esp_hw_support esp_system freertos log
)
