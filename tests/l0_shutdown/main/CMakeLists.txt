idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES freertos log m5sw_board m5sw_power m5sw_test_support
)
