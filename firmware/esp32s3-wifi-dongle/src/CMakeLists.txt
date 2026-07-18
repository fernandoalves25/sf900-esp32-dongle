set(srcs)

list(APPEND srcs
    "usb_dongle_main.c"
    "CLI_Commands.c"
    "Command_Parse.c"
    "cmd_wifi.c"
    "data_back.c"
    "led_status.c"
    )

if(CONFIG_UART_ENABLE)
    list(APPEND srcs
        "uart.c")
endif() # CONFIG_UART_ENABLE

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS .
                       REQUIRES tinyusb_dongle esp_wifi nvs_flash console driver app_update)
