# M4 主组件：电源锁存 + 板级 LCD/音频 + 8 fps 主循环 + 串口注入 + 按键采样 + NVS。
idf_component_register(
    SRCS "main.c"
         "board_lcd.c"
         "board_audio.c"
    INCLUDE_DIRS "."
    EMBED_FILES "assets/voice/input.pcm"
                "assets/voice/done.pcm"
                "assets/voice/failed.pcm"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc
                  esp_driver_i2c esp_driver_i2s
                  esp_driver_usb_serial_jtag esp_timer nvs_flash pet_ui es8311
)
