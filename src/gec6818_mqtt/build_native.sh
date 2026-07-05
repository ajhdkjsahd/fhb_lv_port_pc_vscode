#!/bin/bash
# build_native.sh — 在 WSL/PC 上本地编译 x86_64 调试版本
# 用系统 gcc 编译，直接在 WSL 中运行测试
# 调通后再用 build_paho.sh + build_app.sh 交叉编译给开发板
set -e

PAHO_VER=1.3.13
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR"

WORK_DIR=${SCRIPT_DIR}/build_work_native
PAHO_DIR=${SCRIPT_DIR}/paho-install-native
APP_DIR=${SCRIPT_DIR}/app_build_native

echo "=========================================="
echo " 本地编译 (x86_64) — WSL 调试用"
echo "=========================================="

# 依赖检查
for cmd in cmake git gcc; do
    if ! command -v $cmd &>/dev/null; then
        echo "[提示] 正在安装 $cmd ..."
        sudo apt install -y $cmd
    fi
done
echo "[OK] cmake git gcc 就绪"

# --- 1. 编译 Paho MQTT C 库 ---
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Paho 分两层：paho-mqtt3c (MQTTClient 同步 API) 依赖 paho-mqtt3a (MQTTAsync 异步底层)
# 两个库都需要链接
PAHO_LIB_C=$(ls "${PAHO_DIR}"/lib/libpaho-mqtt3c*.a 2>/dev/null | head -1)

if [ -z "$PAHO_LIB_C" ]; then
    if [ ! -d "paho.mqtt.c" ]; then
        echo ""
        echo ">>> [1/3] 下载 Paho MQTT C v${PAHO_VER} ..."
        git clone -b v${PAHO_VER} https://github.com/eclipse/paho.mqtt.c.git
    fi
    echo ""
    echo ">>> [2/3] 编译 Paho (x86_64) ..."
    cd paho.mqtt.c
    rm -rf build && mkdir build && cd build
    cmake .. \
        -DCMAKE_INSTALL_PREFIX="${PAHO_DIR}" \
        -DPAHO_WITH_SSL=OFF \
        -DPAHO_BUILD_STATIC=ON \
        -DPAHO_BUILD_SHARED=OFF \
        -DPAHO_BUILD_DOCUMENTATION=OFF \
        -DPAHO_BUILD_SAMPLES=OFF \
        -DPAHO_ENABLE_TESTING=OFF
    make -j$(nproc)
    make install
    echo ""
    echo "[OK] Paho 安装完成"
    # 显示实际生成的文件
    echo "  lib 内容:"
    ls -la "${PAHO_DIR}/lib/"
    echo "  include 内容:"
    ls "${PAHO_DIR}/include/"
else
    echo ""
    echo "[跳过] Paho 库已存在:"
    ls -la "${PAHO_DIR}/lib/libpaho-mqtt3"*
fi

# 查找所需库文件
PAHO_LIB_C=$(ls "${PAHO_DIR}"/lib/libpaho-mqtt3c*.a 2>/dev/null | head -1)
PAHO_LIB_A=$(ls "${PAHO_DIR}"/lib/libpaho-mqtt3a*.a 2>/dev/null | head -1)

if [ -z "$PAHO_LIB_C" ]; then
    echo ""
    echo "[错误] 没找到 libpaho-mqtt3c*.a (MQTTClient API)"
    echo "请检查:"; echo "  ls ${PAHO_DIR}/lib/"
    exit 1
fi
if [ -z "$PAHO_LIB_A" ]; then
    echo ""
    echo "[错误] 没找到 libpaho-mqtt3a*.a (MQTTAsync 底层)"
    echo "请检查:"; echo "  ls ${PAHO_DIR}/lib/"
    exit 1
fi

# --- 2. 编译应用 ---
echo ""
echo ">>> [3/3] 编译 MQTT 应用 (x86_64) ..."
echo "  链接 C 库: ${PAHO_LIB_C}"
echo "  链接 A 库: ${PAHO_LIB_A}"
mkdir -p "$APP_DIR"
cd "$APP_DIR"

# 链接顺序有讲究：mqtt3c (上层API) 依赖 mqtt3a (底层)，所以 c 在前 a 在后
gcc -o mqtt_sub "${SCRIPT_DIR}/mqtt_sub.c" \
    -I"${PAHO_DIR}/include" \
    "${PAHO_LIB_C}" "${PAHO_LIB_A}" \
    -lpthread -ldl -std=c99 -O0 -g

gcc -o mqtt_pub_demo "${SCRIPT_DIR}/mqtt_pub_demo.c" \
    -I"${PAHO_DIR}/include" \
    "${PAHO_LIB_C}" "${PAHO_LIB_A}" \
    -lpthread -ldl -std=c99 -O0 -g

echo ""
echo "=========================================="
echo " 编译完成！(x86_64 调试版)"
echo "=========================================="
echo ""
echo " 可执行文件（可直接在 WSL 中运行）："
file mqtt_sub mqtt_pub_demo
echo ""

echo " ┌─────────────────────────────────────────┐"
echo " │  调试流程                                │"
echo " │                                         │"
echo " │  终端1:  cd app_build_native            │"
echo " │          ./mqtt_sub                     │"
echo " │                                         │"
echo " │  终端2:  mosquitto_pub \\                │"
echo " │    -h broker.emqx.io -p 1883 \\          │"
echo " │    -t gec6818/test/data -m 'hello'      │"
echo " │                                         │"
echo " │  终端1 应显示: "收到消息: hello"          │"
echo " │                                         │"
echo " │  调通后交叉编译:                         │"
echo " │    ./build_paho.sh && ./build_app.sh    │"
echo " └─────────────────────────────────────────┘"
