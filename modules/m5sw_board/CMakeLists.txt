idf_component_register(
    SRCS "m5sw_board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES M5IOE1 driver freertos i2c_bus
)
