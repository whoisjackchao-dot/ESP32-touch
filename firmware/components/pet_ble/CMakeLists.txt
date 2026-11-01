# pet_ble：M5b-2 BLE GATT 事件通道（C09）—— NimBLE peripheral + Nordic UART Service。
# 行重装纯逻辑 pet_ble_lines.c 复用 pet_ui 的 pet_linebuf（未改动）；跨通道去重门
# 复用 pet_net 的 pet_net_line_accept（C08 §1.1，USB/WiFi/BLE 共用一个 1 s 指纹环）。
idf_component_register(
    SRCS "pet_ble.c"
         "pet_ble_lines.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_timer pet_net pet_ui
)
