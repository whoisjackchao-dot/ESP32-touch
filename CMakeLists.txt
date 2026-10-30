# esp32-buddy-touch154 M1：真正的工程在 firmware/。
# 本文件让验收命令可以在仓库根直接执行：
#   idf.py -B ~/esp-build/buddy154 -DIDF_TARGET=esp32s3 build
# 组件、分区表、sdkconfig.defaults 全部取自 firmware/，无源码副本；
# root 下的 partitions.csv 是 configure 阶段生成的派生文件（勿手改）。
# 在 firmware/ 目录内构建同样可用（canonical 入口）。
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/firmware/components"
    "${CMAKE_CURRENT_LIST_DIR}/firmware/main")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/firmware/sdkconfig.defaults")
# 自定义分区表由 Kconfig 相对 PROJECT_DIR（此处=仓库根）解析。
configure_file("${CMAKE_CURRENT_LIST_DIR}/firmware/partitions.csv"
               "${CMAKE_CURRENT_LIST_DIR}/partitions.csv" COPYONLY)
# 生成的 sdkconfig 放进构建目录，保持仓库根干净。
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(buddy154)
