# M2 主组件：电源锁存入口 + 板级 LCD 层 + 8 fps 主循环 + 串口注入通道。
idf_component_register(
    SRCS "main.c"
         "board_lcd.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc
                  esp_driver_usb_serial_jtag esp_timer pet_ui
)
