# pet_ui：上游 ESP32-buddy（MIT）components/pet 的渲染子集 + 本项目 M2 增量。
# 上游：pet_render.c / pet_text.c（逐字节复制，来源见 NOTICE.md）。
# 本项目新增：pet_state.c（状态机，按 C03 修订卡栈规则）、pet_ui_240.c（240×240 版式）。
# 立绘包 assets/robot.look 随组件嵌入（_binary_robot_look_start/_end）。
# 来源与许可登记：本目录 NOTICE.md；对应本项目契约 C01/C02/C03、docs/08-m2-development.md。
idf_component_register(
    SRCS "pet_render.c"
         "pet_text.c"
         "pet_state.c"
         "pet_ui_240.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/robot.look"
)
