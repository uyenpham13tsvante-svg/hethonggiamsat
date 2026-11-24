idf_component_register(
    SRCS "main.c" "lcd_btn.c" "actuators.c" "sensors.c" "storage.c" "wifi_tg.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_adc esp_rom esp_system nvs_flash esp_wifi esp_event esp_netif esp_http_client mbedtls
)