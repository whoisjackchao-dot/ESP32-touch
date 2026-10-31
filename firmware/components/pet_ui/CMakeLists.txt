# pet_ui：上游 ESP32-buddy（MIT）components/pet 的渲染/逻辑子集 + 本项目增量。
# 上游（逐字节/派生，来源见 NOTICE.md）：pet_render.c / pet_text.c / pet_button.c /
#   pet_menu.c / pet_pomo.c（派生：时长可运行期覆盖、统计 NVS 拆分）；
# 本项目新增：pet_state.c（M2 状态机）、pet_ui_240.c（M2 值班页）、
#   pet_keymap.c（M3 键表）、pet_pomo_store.c（M3 NVS）、
#   pet_ui_pomo_240.c / pet_ui_menu_240.c（M3 两页）。
# 立绘包 assets/robot.look 随组件嵌入（_binary_robot_look_start/_end）。
idf_component_register(
    SRCS "pet_render.c"
         "pet_text.c"
         "pet_button.c"
         "pet_menu.c"
         "pet_pomo.c"
         "pet_pomo_store.c"
         "pet_state.c"
         "pet_keymap.c"
         "pet_ui_240.c"
         "pet_ui_pomo_240.c"
         "pet_ui_menu_240.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/robot.look"
    REQUIRES nvs_flash
)
