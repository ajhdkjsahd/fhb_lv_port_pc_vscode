# 智慧水产养殖系统 — LVGL v9 UI

基于 **LVGL v9** 的智慧水产养殖管理系统，支持 PC 模拟（SDL2）和 **GEC6818 ARM Linux** 嵌入式板卡双平台运行。

---

## 功能模块

| 模块 | 说明 |
|------|------|
| 登录/注册 | 账号密码验证、注册、成功弹窗动画 |
| **首页仪表盘** | **海洋沉浸式导航枢纽 — canvas 双层波浪、气泡上浮、鱼影游动、玻璃质感欢迎卡、霓虹药丸导航坞、WiFi 网络状态指示器、离屏自动暂停优化** |
| **传感器数据监测** | **6 路传感器实时展示（温度/湿度/光照/溶解氧/pH/氨氮），三态告警配色，光照卡片发光特效，实时模拟 + 硬件接入钩子** |
| **AI 智能助手** | DeepSeek-R1:7B 对话、思考过程折叠、Ollama HTTP、流式显示 |
| 视频监控 | mplayer 视频播放、滑动切换视频、封面预览、进度条 |
| 图片浏览 | 文件夹自动扫描、滑动切换、圆点指示器、缓存预加载 |
| 网络通讯 | TCP Socket 客户端、收发消息、在线终端显示 |
| 拼音输入法 | 集成 lv_100ask_pinyin_ime，支持中文拼音输入 |

---

## 最新更新 (2026-07)

### 首页重构 — 海洋沉浸式导航枢纽

全新设计的首页，深海蓝 + 青色霓虹主题，多项动态视觉效果：

| 元素 | 实现方式 |
|------|---------|
| 深海渐变背景 | 垂直渐变 `#0A2640` → `#02060E` + 顶部青色光晕 |
| 玻璃欢迎卡 | 半透明 bg + 渐变 + 青色边框 + 50px 外发光阴影 |
| 双层波浪 | `lv_canvas` 800×80 ARGB8888，40ms 定时器直接写像素重绘正弦波（后层蓝 + 前层青，反向流动） |
| 气泡 ×12 | `lv_obj` 小圆 + `lv_anim` 上浮 + 淡入淡出，错峰参数表 |
| 鱼影 ×3 | fa-fish 字形 label + `lv_anim` 横移 + sin 摆尾，不同深度/速度/颜色/透明度 |
| 导航药丸 ×5 | 5 个玻璃质感药丸（视频/图片/网络/AI/传感器），悬停上浮发光 + 点击回调 |
| 网络状态指示 | 顶部状态药丸，颜色随 WiFi 检测结果变化（绿/黄/红） |
| 离屏暂停优化 | `LV_EVENT_SCREEN_LOAD/UNLOAD` 生命周期管理，离开首页自动暂停波浪/WiFi 定时器 |

### 传感器数据监测页

6 路水产关键传感器，3 行 × 2 列卡片网格，与现有页面风格统一（海洋深色主题）：

| 传感器 | 单位 | 图标 | 安全带 | 告警 |
|--------|------|------|--------|------|
| 温度 | °C | fa-temperature-half | 18–28 | <15 / >32 |
| 湿度 | % | fa-droplet | 40–80 | <30 / >90 |
| 光照 | % | fa-sun | 20–90 | 随值发光（值越大卡片暖黄光晕越亮） |
| 溶解氧 | mg/L | fa-wind | 5–20 | <3 |
| pH值 | — | fa-vial | 6.5–8.5 | <6 / >9 |
| 氨氮 | mg/L | fa-flask | <0.5 | >1.0 |

- **三态告警配色**：正常青绿 / 预警黄 / 告警红，数值越限时边框、图标、状态点、进度条、数值颜色同步变化
- **实时模拟**：`app_action_sensor_read()` 提供随机游走模拟数据，1 秒刷新
- **硬件钩子**：接入真实传感器只需替换 `app_actions.c` 中 `app_action_sensor_read()` 的取值实现

---

## 截图

<img src="src/page1.png" width="600" alt="登录 & 首页">

*登录页面 & 首页仪表盘 — 海洋沉浸式导航枢纽*

<img src="src/page2.png" width="600" alt="视频监控 & 图片浏览">

*视频监控（mplayer 嵌入播放 + 滑动切换）& 图片浏览（文件夹扫描 + 缓存预加载）*

<img src="src/page3.png" width="600" alt="网络通讯 & 中文输入">

*网络通讯页面（Socket 收发 + 终端消息区）& 拼音中文输入键盘*

<img src="src/page4.png" width="600" alt="AI 智能助手">

*AI 智能助手 — DeepSeek-R1:7B 大模型对话、思考过程可折叠、头像呼吸灯光晕、快捷提问按钮*

<img src="src/page5.png" width="600" alt="传感器数据监测">

*传感器数据监测页 — 6 路传感器实时展示、三态告警配色、光照卡片发光特效*

---

## 目录结构

```
src/ui-smart-water/
├── ui.h / ui.c                  ← 入口，屏幕创建 + 导航
├── preview/                     ← HTML 预览文件（首页重构 + 传感器页）
├── fonts/                       ← SIMKAI.TTF（楷体）+ FA6-Free-Solid-900.otf（图标）
├── images/                      ← 图片资源（.png）
└── pages/
    ├── app_fonts.c/h            ← FreeType 字体加载（kaiti_14/18/24/32 + fa6_20）
    ├── app_actions.c/h          ← 业务逻辑回调（登录、视频、网络、WiFi、传感器模拟）
    ├── app_keyboard.c/h         ← 拼音输入法键盘
    ├── app_popup.c/h            ← Toast 弹窗
    ├── register-page/           ← 登录 + 注册页面
    ├── home-page/               ← 首页（海洋沉浸式导航枢纽）
    ├── sensor-page/             ← 传感器数据监测页
    ├── video-page/              ← 视频监控页面
    ├── gallery-page/            ← 图片浏览页面
    ├── network-page/            ← 网络通讯页面
    ├── ai-chat-page/            ← AI 智能助手页面（Ollama / DeepSeek-R1）
    └── pinyin-ime/              ← lv_100ask_pinyin_ime（适配版）
```

---

## 编译运行

### PC（Windows / Linux）

```bash
mkdir build && cd build
cmake .. -DLV_USE_FREETYPE=ON
cmake --build .
cd bin && ./main
```

预置账号：`a` / `a`

### GEC6818 ARM Linux 板卡

交叉编译后将 `bin/main` 和字体文件部署到板卡：

```bash
# 部署
cp bin/main /root/
cp src/ui-smart-water/fonts/SIMKAI.TTF /root/
cp src/ui-smart-water/fonts/FA6-Free-Solid-900.otf /root/

# 视频文件放在 /root/videos/
# 图片文件放在 /root/images/

# 运行
cd /root && ./main
```

> **注意**：板子编译时 `app_fonts.c` 中字体路径会自动切换为 `/root/fonts/SIMKAI.TTF` 和 `/root/fonts/FA6-Free-Solid-900.otf`。

> **ARM 兼容提示**：首页动画在 GEC6818 上已长期稳定运行验证。板子的 fbdev 帧缓冲不兜底越界坐标，代码中已避免了 `transform_scale` 在 label 上的使用（已知会在 ARM 板上段错误），所有动画均使用 `lv_anim` + `lv_canvas`（安全）实现。

---

## 设计风格

### 首页 — 深海蓝 + 青色霓虹（旗舰沉浸主题）

| 属性 | 色值 | 用途 |
|------|------|------|
| 顶部 | `#0A2640` | 深蓝（模拟水面光） |
| 底部 | `#02060E` | 近黑（深海） |
| 霓虹 | `#00E5FF` | 边框、分割线、图标、悬浮发光 |
| 青绿 | `#00D4AA` | 渐变色、健康状态 |
| 蓝色 | `#0288D1` | 渐变色、后层波浪 |
| 文字 | `#E6F2EE` | 标题、正文 |

### 子页面 — Ocean / Aquatic 暗色主题

| 属性 | 色值 | 用途 |
|------|------|------|
| 背景 | `#060E14` | 页面主背景 |
| 卡片 | `#0A1620` | 容器、面板 |
| 主色调 | `#00D4AA` | 按钮渐变、强调 |
| 辅色调 | `#0288D1` | 渐变、接收消息 |
| 文字主色 | `#E0E0E0` | 标题 |
| 文字辅色 | `#9AB8B0` | 正文 |
| 文字弱色 | `#5A7A72` | 提示 |

**字体**：楷体（SIMKAI.TTF）+ Font Awesome 6 图标（FA6-Free-Solid-900.otf）

---

## AI 智能助手

通过 Ollama 服务器与 **DeepSeek-R1:7B** 大模型对话，纯 POSIX socket 实现 HTTP 客户端，零外部库依赖。支持 PC 模拟和 ARM Linux 板卡。

```
LVGL 主线程
  ├─ 发送消息 → pthread_create → ai_recv_thread
  ├─ ai_recv_thread → http_post() → /api/chat
  ├─ 解析 JSON → 分离 <think> 思考过程
  ├─ lv_async_call → 页面更新（思考面板 + 回答气泡）
  └─ 头像呼吸灯光晕动画
```

| 功能 | 说明 |
|------|------|
| 思考过程 | `<think>` 标签自动拆分，可折叠面板 |
| 对话历史 | 环形缓冲区，最多 100 轮上下文 |
| 思考中提示 | 发送后立即显示"AI 正在思考…"占位 |
| 停止生成 | 可中途打断 AI 回复 |
| 清空对话 | 一键清除所有消息 |
| 快捷提问 | 4 个预设水产养殖问题 |
| 头像光晕 | 青绿色呼吸灯光晕脉动动画 |

**Ollama 配置**（`app_actions.c` 中修改）：

```c
#define OLLAMA_HOST    "192.168.137.1"   // WSL IP
#define OLLAMA_PORT    11434
#define OLLAMA_MODEL   "deepseek-r1:7b"
#define OLLAMA_TMO     300               // 超时秒数
```

PC 端 Ollama 客户端源码参考：`src/gec6818_ollama_client/`

---

## 页面跳转动画

| 方向 | 动画 | 时长 |
|------|------|------|
| 进入子页面 | 左滑推入 | 350ms |
| 返回上层 | 右滑退出 | 350ms |
| 登录成功 | 淡入 | 400ms |

---

## 网络通讯

TCP 客户端直接集成在 LVGL 内，无需额外进程：

```
LVGL 主线程
  ├─ 点「连接」→ socket() → connect() → pthread recv_thread
  ├─ 点「发送」→ write(sock, msg)
  ├─ recv_thread → lv_async_call → 消息区更新
  └─ 点「断开」→ shutdown() → recv_thread 退出
```

可用指令（发送到服务端）：

| 指令 | 说明 |
|------|------|
| `@list` | 查看在线用户列表 |
| `@name 新名字` | 修改自己的名字 |
| `@all 消息` | 广播消息给所有人 |
| `@目标 消息` | 发送给指定用户 |
| 普通消息 | 服务端日志记录 |

---

## License

MIT
