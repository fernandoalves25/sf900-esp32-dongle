idf_component_register(SRCS "main.c" "ble_hid.c" "led_rx.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver app_update esp_hid bt)
