idf_component_register(SRCS "main.c" "wifi_sta.c" "stream.c" "gate.c" "link_stats.c" "ui.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_timer esp_psram esp_wifi esp_netif nvs_flash
                                     esp_driver_i2c esp_driver_tsens lcd1602)
