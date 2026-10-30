# pet_ui：上游 ESP32-buddy（MIT）components/pet 的最小渲染子集。
# 只保留 M1 需要的立绘解析/绘制（pet_render.c）与 8×8 ASCII 文本（pet_text.c）；
# 明确不含 pet_hal_sticks3 / pm1 / pet_audio / pet_keys / pet_net / pet_ble。
# 立绘包 assets/robot.look 随组件嵌入（_binary_robot_look_start/_end）。
# 来源与许可登记：本目录 NOTICE.md；对应本项目契约 C01/C02。
idf_component_register(
    SRCS "pet_render.c"
         "pet_text.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/robot.look"
)
