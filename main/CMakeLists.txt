idf_component_register(SRCS "ui_thermostat_icon" "ui_img_clock_icon.c" "backg_room1.c" "floor_lamp.c" "esp32-s3-touch-lcd-ha-dashboard.c" "wifi_init.c" "wifi_config.c" "mqtt_config.c" "lamp_config.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi esp_event esp_netif mqtt )
