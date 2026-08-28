idf_component_register(
    SRCS "m5sw_wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_wifi freertos log lwip nvs_flash
)
