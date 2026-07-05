#!/bin/bash
# 编译 MQTT 应用程序（mqtt_sub / mqtt_pub_demo）
# 针对 GEC6818 Buildroot 2016.11: arm-linux-gcc
# 在 WSL 中运行此脚本

set -e

INSTALL_DIR=$(pwd)/paho-install
BUILD_DIR=$(pwd)/app_build

CROSS_PREFIX=arm-linux-

# 检查 Paho 库是否已编译
if ! ls ${INSTALL_DIR}/lib/libpaho-mqtt3*.a &>/dev/null; then
    echo "[错误] 未找到 Paho MQTT 库"
    echo "请先在 WSL 中运行:  ./build_paho.sh"
    exit 1
fi

echo "=========================================="
echo " 编译 MQTT 应用程序"
echo " 编译器: ${CROSS_PREFIX}gcc"
echo "=========================================="

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

cmake .. \
    -DCMAKE_C_COMPILER=${CROSS_PREFIX}gcc

make -j$(nproc)

echo ""
echo "=========================================="
echo " 编译完成！"

# 检查是否静态链接
echo ""
echo ">>> 文件信息："
file mqtt_sub mqtt_pub_demo
echo ""

echo "  订阅程序: ${BUILD_DIR}/mqtt_sub"
echo "  发布程序: ${BUILD_DIR}/mqtt_pub_demo"
echo ""
echo "  部署到开发板："
echo "    chmod +x mqtt_sub mqtt_pub_demo"
echo "    ./mqtt_sub"
echo ""
echo "  (把可执行文件从 WSL 的 /mnt/c/Users/熊大/WorkBuddy/... 下"
echo "   通过 tftp/nfs/U盘 拷到开发板即可)"
