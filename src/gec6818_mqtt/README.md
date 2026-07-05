# GEC6818 开发板 MQTT 数据获取方案

基于 Eclipse Paho MQTT C 库，在 WSL 中交叉编译后部署到 GEC6818 开发板，
连接公共测试 broker 实现 MQTT 数据订阅/发布。

## 环境现状

| 项目 | 情况 |
|------|------|
| 开发板联网 | ✅ 已能 ping 通外网 |
| 交叉编译器 | `arm-linux-gcc`（Buildroot 2016.11, arm-none-linux-gnueabi, Cortex-A15） |
| 编译环境 | WSL 2（Ubuntu） |
| MQTT Broker | broker.emqx.io:1883（公共测试，零配置） |

## 推荐工作流

```
修改代码 → 本地编译调试 → 测试OK → 交叉编译 → 部署到开发板
  ↑                                                  │
  └──────────────── 迭代优化 ←────────────────────────┘
```

## 一、本地调试（推荐先做）

在 WSL 中用系统 gcc 编译 x86_64 版本，直接在 WSL 中跑通，无需开发板参与。

```bash
cd /mnt/c/Users/熊大/WorkBuddy/2026-07-04-15-22-14/gec6818_mqtt/
chmod +x build_native.sh
./build_native.sh
```

编译产物在 `app_build_native/`，可直接在 WSL 中运行。

**测试：**

```bash
# 终端1：启动订阅
cd app_build_native && ./mqtt_sub

# 终端2：发布测试消息（需要 mosquitto-clients）
sudo apt install -y mosquitto-clients
mosquitto_pub -h broker.emqx.io -p 1883 -t "gec6818/test/data" -m "hello"
```

终端1 应立即显示 `收到消息: hello`。

## 二、交叉编译部署（调通后做）

### 第 1 步：交叉编译 Paho MQTT C 库

```bash
chmod +x build_paho.sh
./build_paho.sh
```

自动完成：下载 Paho 源码 → `arm-linux-gcc` 交叉编译 → 安装到 `./paho-install/`。

### 第 2 步：编译应用程序

```bash
chmod +x build_app.sh
./build_app.sh
```

生成两个 ARM 可执行文件：
- `app_build/mqtt_sub` — 订阅消息（接收数据）
- `app_build/mqtt_pub_demo` — 发布消息（发数据）

### 第 3 步：部署到开发板

把可执行文件从 WSL 目录拷到开发板（任选一种）：

```bash
# 方式 A：tftp
tftp <开发板IP> -p -b 8192 -r mqtt_sub

# 方式 B：nfs 挂载（推荐调试阶段用）
# 方式 C：U 盘 / SD 卡直接拷贝
```

### 第 4 步：开发板上运行

```bash
chmod +x mqtt_sub
./mqtt_sub
```

留意输出 `连接成功!` 和 `订阅成功, 等待消息...`。

## 自定义配置

### 换 Broker

修改 `mqtt_sub.c` 顶部宏：

```c
#define BROKER_ADDRESS "tcp://你的broker地址:1883"
#define TOPIC       "你的主题"
```

重新运行 `./build_app.sh`。

### 添加 TLS/SSL

1. `build_paho.sh` 中 `PAHO_WITH_SSL` 改为 `ON`
2. 交叉编译前确保有 OpenSSL 的交叉编译库
3. 代码中 broker 地址改为 `ssl://...:8883`

## 文件说明

| 文件 | 说明 |
|------|------|
| `mqtt_sub.c` | 订阅程序源码 |
| `mqtt_pub_demo.c` | 发布程序源码（模拟传感器数据） |
| `CMakeLists.txt` | 应用构建配置 |
| `toolchain.cmake` | 交叉编译工具链文件 |
| `build_native.sh` | **本地编译 x86_64 调试版**（WSL 直接跑） |
| `build_paho.sh` | 交叉编译 Paho 库（arm-linux-gcc） |
| `build_app.sh` | 编译 ARM 应用程序 |

## 常见问题

**Q：连接 broker 超时？**
```bash
# 在开发板上先测试
ping broker.emqx.io
echo | nc broker.emqx.io 1883 -w 3
```
如果 ping 通但 1883 不通，可能是网络防火墙封了非标准端口，换个 broker 或用自己的。

**Q：运行报 "not found"？**
确认可执行文件权限 `chmod +x mqtt_sub`，且部署到的是**开发板**而非 PC。

**Q：交叉编译报错找不到头文件？**
确保先跑 `./build_paho.sh` 生成 `paho-install/` 目录。
