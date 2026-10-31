# pet_net：M5b-1 WiFi 事件通道（C08）—— STA + AP 配网 + TCP :8766 + SNTP + 跨通道去重。
# 只依赖 IDF 5.5 自带组件；pet_linebuf 复用 pet_ui 的行缓冲实现（未改动）。
idf_component_register(
    SRCS "pet_net.c"
         "pet_net_dedupe.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash lwip esp_timer
             pet_ui
)
