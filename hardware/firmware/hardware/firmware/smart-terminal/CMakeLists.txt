# 智守云伴 · ESP32-S3 智能终端（单板全功能固件）
#
# 目标：ESP32-S3（8MB Flash + 八线 PSRAM）
# 环境：ESP-IDF v5.2.2（见 README 的版本要求）
#
# 构建：
#   idf.py set-target esp32s3
#   idf.py build
#   idf.py -p COM3 flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(smart-terminal)
