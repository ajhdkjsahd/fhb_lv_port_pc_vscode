#!/bin/bash
# 交叉编译 Paho MQTT C 库（静态，无 SSL）
# 针对 GEC6818 Buildroot 2016.11: arm-none-linux-gnueabi (soft-float)
# 在 WSL 中运行此脚本

set -e

# ===== 配置区 =====
CROSS_PREFIX=arm-linux-

PAHO_VERSION=1.3.13
WORK_DIR=$(pwd)/build_work
INSTALL_DIR=$(pwd)/paho-install
# ==================

echo "=========================================="
echo " Paho MQTT C 交叉编译"
echo " 编译器前缀: ${CROSS_PREFIX}"
echo " 目标:       arm-none-linux-gnueabi"
echo " 安装目录:   ${INSTALL_DIR}"
echo "=========================================="

# 检查编译器
if ! command -v ${CROSS_PREFIX}gcc &>/dev/null; then
    echo "[错误] 找不到交叉编译器 ${CROSS_PREFIX}gcc"
    echo "请确认 arm-linux-gcc 在 WSL 的 PATH 中"
    exit 1
fi
echo "[OK] 编译器: $(${CROSS_PREFIX}gcc --version | head -1)"

# 检查 cmake
if ! command -v cmake &>/dev/null; then
    echo "[提示] 正在安装 cmake ..."
    sudo apt install -y cmake
fi

# 检查 git
if ! command -v git &>/dev/null; then
    echo "[提示] 正在安装 git ..."
    sudo apt install -y git
fi

mkdir -p ${WORK_DIR}
cd ${WORK_DIR}

# 1. 下载 Paho MQTT C 源码
if [ ! -d "paho.mqtt.c" ]; then
    echo ""
    echo ">>> [1/3] 下载 paho.mqtt.c v${PAHO_VERSION} ..."
    git clone -b v${PAHO_VERSION} https://github.com/eclipse/paho.mqtt.c.git
else
    echo ""
    echo ">>> [1/3] paho.mqtt.c 已存在，跳过下载"
fi

# 2. 交叉编译
echo ""
echo ">>> [2/3] 交叉编译 paho.mqtt.c ..."
cd paho.mqtt.c
rm -rf build && mkdir build && cd build

cmake .. \
    -DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -DPAHO_WITH_SSL=OFF \
    -DPAHO_BUILD_STATIC=ON \
    -DPAHO_BUILD_SHARED=OFF \
    -DPAHO_BUILD_DOCUMENTATION=OFF \
    -DPAHO_BUILD_SAMPLES=OFF \
    -DPAHO_ENABLE_TESTING=OFF

make -j$(nproc)
make install

# 3. 验证
echo ""
echo ">>> [3/3] 验证安装 ..."
PAHO_LIB_A=$(ls ${INSTALL_DIR}/lib/libpaho-mqtt3a*.a 2>/dev/null | head -1)
PAHO_LIB_C=$(ls ${INSTALL_DIR}/lib/libpaho-mqtt3c*.a 2>/dev/null | head -1)

if [ -n "$PAHO_LIB_A" ] && [ -n "$PAHO_LIB_C" ]; then
    echo "[成功] A 库: ${PAHO_LIB_A}"
    echo "[成功] C 库: ${PAHO_LIB_C}"
    echo "[成功] 头文件: ${INSTALL_DIR}/include/MQTTClient.h"
    echo ""
    file $PAHO_LIB_A
    echo ""
    echo "Paho MQTT C 交叉编译完成！"
    echo "接下来运行 ./build_app.sh 编译应用程序"
else
    echo "[错误] 静态库未生成，请检查编译日志"
    echo "  ls ${INSTALL_DIR}/lib/"
    exit 1
fi
