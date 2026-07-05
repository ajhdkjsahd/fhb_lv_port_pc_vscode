# CMakeLists.txt - GEC6818 MQTT 应用程序构建配置
# 支持 x86_64 本地编译 (build_native.sh) 和 ARM 交叉编译 (build_app.sh)

cmake_minimum_required(VERSION 3.5)
project(gec6818_mqtt C)

set(CMAKE_C_STANDARD 99)

# Paho MQTT C 安装路径（由 build_paho.sh 或 build_native.sh 生成）
# 交叉编译时用 paho-install/，本地编译时用 paho-install-native/
# 可通过 -DPAHO_DIR=... 在命令行指定
if(NOT DEFINED PAHO_DIR)
    set(PAHO_DIR ${CMAKE_SOURCE_DIR}/paho-install)
endif()

# 查找库文件（兼容 -static 后缀有无的情况）
file(GLOB PAHO_LIB_C "${PAHO_DIR}/lib/libpaho-mqtt3c*.a")
file(GLOB PAHO_LIB_A "${PAHO_DIR}/lib/libpaho-mqtt3a*.a")

if(NOT PAHO_LIB_C)
    message(FATAL_ERROR "未找到 libpaho-mqtt3c*.a，请先编译 Paho: PAHO_DIR=${PAHO_DIR}")
endif()
if(NOT PAHO_LIB_A)
    message(FATAL_ERROR "未找到 libpaho-mqtt3a*.a，请先编译 Paho: PAHO_DIR=${PAHO_DIR}")
endif()

message(STATUS "PAHO LIB C: ${PAHO_LIB_C}")
message(STATUS "PAHO LIB A: ${PAHO_LIB_A}")

# 订阅程序
add_executable(mqtt_sub mqtt_sub.c)
target_include_directories(mqtt_sub PRIVATE ${PAHO_DIR}/include)
target_link_libraries(mqtt_sub ${PAHO_LIB_C} ${PAHO_LIB_A} pthread dl)

# 发布程序
add_executable(mqtt_pub_demo mqtt_pub_demo.c)
target_include_directories(mqtt_pub_demo PRIVATE ${PAHO_DIR}/include)
target_link_libraries(mqtt_pub_demo ${PAHO_LIB_C} ${PAHO_LIB_A} pthread dl)
