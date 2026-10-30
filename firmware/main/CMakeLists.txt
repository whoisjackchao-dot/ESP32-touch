# M1 主组件：电源锁存入口 + 板级 LCD 层 + 一帧渲染。
idf_component_register(
    SRCS "main.c"
         "board_lcd.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc pet_ui
)
