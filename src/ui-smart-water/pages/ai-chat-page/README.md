# AI 对话 — Qwen2.5:7b 原生 Function Calling 完整技术手册

> **版本**: v3.0 (FC 原生) · **最后更新**: 2026-07-07  
> **模型**: Qwen2.5:7b (Ollama) · **平台**: GEC6818 ARM Linux + PC/Windows 模拟

---

## 目录

1. [架构概览](#一架构概览)
2. [文件清单](#二文件清单)
3. [数据流详解](#三数据流详解)
4. [工具注册表 (10 个硬件/传感器工具)](#四工具注册表)
5. [系统提示词设计](#五系统提示词设计)
6. [多轮工具调用闭环](#六多轮工具调用闭环)
7. [平台适配 (PC 模拟 / ARM 真机)](#七平台适配)
8. [UI 事件桥接 (线程安全)](#八ui-事件桥接)
9. [快捷提问设计](#九快捷提问设计)
10. [如何新增一个工具](#十如何新增一个工具)
11. [CMake 构建配置](#十一cmake-构建配置)
12. [板端部署](#十二板端部署)
13. [常见问题排查](#十三常见问题排查)

---

## 一、架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│ LVGL UI 层 (不变)                                                │
│   ai_chat_page.c  — 聊天气泡 / 输入框 / 快捷提问 / loading       │
│        ↕ send_cb / stop_cb                                       │
│   app_actions.c   — 薄壳 (仅 ~110 行)                            │
│        ↓ ai_agent_send()    ↑ on_event() 回调 (worker 线程触发)  │
├─────────────────────────────────────────────────────────────────┤
│ AI Agent 层 (不依赖 LVGL · 可独立编译)  ★                         │
│   ai_agent.c       — worker 线程 · HTTP · tool_calls 解析循环    │
│   ai_tools.c       — 工具注册表 · 数据驱动 · 新增硬件只加一行    │
│   http_client.c    — POSIX socket HTTP/1.1 (select 超时, ARM 稳) │
│   ai_hardware.c    — 板卡硬件层 (sysfs / GPIO / I2C / input)     │
├─────────────────────────────────────────────────────────────────┤
│ 边缘引擎 (数据层)                                                │
│   edge_engine      — MQTT→清洗→CSV持久化→快照→回归→相关性分析   │
│   ↳ get_latest()   实时快照 (与传感器页/趋势页同一数据源)        │
│   ↳ get_analysis() 回归趋势 + 1h预测 + 当日汇总 + 相关性         │
└─────────────────────────────────────────────────────────────────┘
        ↓ HTTP :11434                     ↓ sysfs / I2C / input
┌──────────────┐              ┌──────────────────────────┐
│ Ollama Server │              │ GEC6818 硬件              │
│ qwen2.5:7b   │              │ LED×4 / 蜂鸣器 / K2按键   │
│ (PC 宿主)     │              │ MMA8653加速度 / 6路传感器 │
└──────────────┘              └──────────────────────────┘
```

**关键设计原则**:
- Agent + Tools **不依赖 LVGL**，可在 PC 上脱离 LVGL 独立编译测试
- 事件驱动: Agent 通过回调发事件，UI 层 (app_actions.c) 用 `lv_async_call` 转到主线程
- 数据驱动: 新增硬件只需在 `g_tools[]` 加一行，不改 Agent 逻辑
- 双版本: `#ifdef __linux__` 真实硬件 + 真实 Ollama; PC 模拟供 UI 调试

---

## 二、文件清单

| 文件 | 行数 | 职责 |
|---|---|---|
| `ai_agent.h` | ~55 | Agent 公开接口: 9 个 API、事件类型、回调签名 |
| `ai_agent.c` | ~340 | Worker 线程 · HTTP POST · tool_calls 解析循环(≤3次) · `<think>`拆分 · 对话历史环形缓冲 |
| `ai_tools.h` | ~50 | 工具注册表接口: handler 类型、注册项结构、dispatch/schema API |
| `ai_tools.c` | ~460 | **10 个真实硬件/传感器工具** · 注册表 · schema 构建 · 值精度四舍五入(与传感器页一致) |
| `ai_hardware.h` | ~75 | 板卡硬件层声明: GPIO 引脚宏、LED 路径、`ai_execute_action` 签名 |
| `ai_hardware.c` | ~581 | 板卡硬件实现: LED sysfs (led1~4) · GPIO28 按键 · GPIO78 蜂鸣器(状态跟踪) · MMA8653 input子系统 · 智能随机开灯 · PC 模拟桩 |
| `http_client.h` | ~25 | HTTP/1.1 POST 客户端接口 |
| `http_client.c` | ~249 | POSIX socket 实现: select 超时 (比 `SO_RCVTIMEO` 更稳) · 仅 `__linux__` 编译 |
| `ai_chat_page.h` | ~55 | AI 对话 UI 接口: 创建/回调/气泡/思考面板/错误 |
| `ai_chat_page.c` | ~830 | AI 对话 UI 实现: 聊天气泡 · 思考面板 · 输入框 · 发送/停止按钮 · 快捷提问 |
| `app_actions.c` | ~110 | **分区4 薄壳**: init/send/stop 转调 Agent + 事件适配回调 (单次 `lv_async_call` 包) |

> `src/gec6818_ai_fc/` 为原始参考骨架 (含独立 PC 测试入口 `main_test.c` 和独立 `http_client`)，不编译进 LVGL 工程。

---

## 三、数据流详解

### 3.1 一次完整的 AI 问答 (以「帮我全面评估当前水质」为例)

```
 用户输入 "帮我全面评估当前水质环境状态"
  │
  ▼
ai_chat_page.c → send_cb → app_action_ai_send(msg)
  │
  ▼
app_actions.c → ai_agent_send(msg)
  │  pthread_create → worker_thread
  ▼
ai_agent.c: worker_thread (__linux__)
  │
  ├─[1] build_body() 拼 JSON:
  │     {model, messages:[system+history+user], tools:[...], stream:false}
  │
  ├─[2] http_chat() → http_post(host, 11434, "/api/chat", body, 300s)
  │     同步阻塞, 等待 Ollama 返回完整 JSON
  │
  ├─[3] cJSON_Parse(resp) → 提取 message.content / message.tool_calls
  │
  ├─[4] 有 tool_calls?
  │     ├─ YES → handle_tool_calls(msg)
  │     │   ├─ 存 assistant 消息进历史 (含 tool_calls 数组)
  │     │   ├─ 遍历 tool_call → ai_tools_dispatch(name, args)
  │     │   │   └─ 调用 ai_execute_action / edge_engine_get_latest
  │     │   ├─ 工具结果带 name 存为 {"role":"tool","name":"...",...} 进历史
  │     │   └─ emit_event(AI_EVT_ACTION) → UI 显示 "[执行 read_water_quality] 结果"
  │     │   └─ continue → 回到 [2] 再请求模型 (最多循环 3 次)
  │     │
  │     └─ NO → content 是最终回复
  │         ├─ emit_thinking_if_any() — 拆分 <think> 块
  │         ├─ emit_event(AI_EVT_ANSWER) — 最终回复
  │         └─ hist_add("assistant", content) — 存入历史
  │
  ├─[5] emit_event(AI_EVT_DONE)
  │
  ▼
app_actions.c: on_event() 回调 (worker 线程)
  │  累计 thinking + action 行 + answer + error 到 ai_ui_packet_t
  │  AI_EVT_DONE 时 → 单次 lv_async_call(ai_ui_show, pkt)
  │
  ▼
LVGL 主线程: ai_ui_show(pkt)
  │  ai_chat_page_begin_response()    — 显示 AI 气泡
  │  ai_chat_page_append_thinking()   — 思考面板 (含工具调用行)
  │  ai_chat_page_finish_thinking()   — 折叠思考面板
  │  ai_chat_page_append_answer()     — 回答文本
  │  ai_chat_page_finish_response()   — 移除加载动画
  │
  ▼
用户看到: 思考面板 "[执行 read_water_quality] ✓" → 回答 "当前水质总体健康..."
```

### 3.2 对话历史格式 (环形缓冲, 最多 100 轮)

```
messages: [
  {"role":"system","content":"你是智慧水产养殖AI助手..."},
  {"role":"user","content":"帮我全面评估当前水质环境状态"},
  {"role":"assistant","content":null,"tool_calls":[{"function":{"name":"read_water_quality",...}}]},  ← hist_add_raw
  {"role":"tool","name":"read_water_quality","content":"{\"ok\":true,\"readings\":[...]}"},          ← hist_add_tool
  {"role":"assistant","content":"当前水质总体健康: 水温 26.5°C...溶氧 4.8 mg/L...建议..."},            ← hist_add
  {"role":"user","content":"那帮我推荐投喂方案"},
  ...下一轮...
]
```

**关键**: assistant `tool_calls` 消息 + tool 结果带 `name` 字段 === 模型能回溯自己的工具调用历史，多轮对话不乱。

---

## 四、工具注册表

### 4.1 工具清单 (10 个)

| # | 工具名 | 参数 | 底层调用 | 用途 |
|---|---|---|---|---|
| 1 | `read_water_quality` | — | `edge_engine_get_latest`×6 | **一次性读全部 6 路水质** + 当日汇总 + 参考区间 |
| 2 | `analyze_environment` | — | `get_latest`×6 + `get_analysis` | **深度分析**: 1h预测 + 趋势方向 + 传感器相关性 + 当日统计 |
| 3 | `read_water_sensor` | `sensor`: temp/humi/light/do/ph/nh3n | `get_latest`×1 | 按需读单路传感器 |
| 4 | `control_led` | `led_id`(1-4), `state`(on/off) | `ai_execute_action("ledN_on/off")` | 控制 D7~D10 LED |
| 5 | `control_all_leds` | `state`(on/off) | `ai_execute_action("led_all_on/off")` | 全部 LED 开/关 |
| 6 | `random_led_on` | — | `ai_execute_action("led_random_on")` | 智能随机点灯 (从灭的中选) |
| 7 | `control_buzzer` | `state`(on/off) | `ai_execute_action("buzzer_on/off")` | 蜂鸣器 GPIO78 |
| 8 | `toggle_buzzer` | — | `ai_execute_action("buzzer_toggle")` | 蜂鸣器反转 (软件状态跟踪) |
| 9 | `read_button` | — | `ai_execute_action("read_button")` | K2 按键 GPIO28 (0=按下) |
| 10 | `read_acceleration` | — | `ai_execute_action("read_accel")` | MMA8653 三轴加速度 ±2g |

### 4.2 数据精度保证

所有传感器数值在存入 JSON 前都会按 `g_sensor_phys[idx].dec` 四舍五入:

| 传感器 | 小数位 | 示例 |
|---|---|---|
| 温度 | 1 | `26.5` (不是 `26.543`) |
| 湿度 | 0 | `68` (不是 `67.83`) |
| 光照 | 0 | `72` |
| 溶解氧 | 1 | `4.8` |
| pH | 2 | `7.18` |
| 氨氮 | 2 | `0.33` |

**与传感器页面和趋势报表的显示值完全一致** — 同一个数据源 (`edge_engine_get_latest`) + 同一个小数位规则。

### 4.3 工具返回 JSON 示例

```json
{
  "ok": true,
  "sensors_available": 6,
  "readings": [
    {
      "sensor": "temperature",
      "unit": "°C",
      "available": true,
      "value": 26.5,
      "timestamp": 1750315200,
      "today_min": 24.8,
      "today_max": 28.1,
      "today_mean": 26.3,
      "reject_count": 2
    },
    ...
  ],
  "reference_ranges": {
    "temperature": "22~30°C (温水鱼), <20°C 生长慢, >32°C 缺氧风险",
    "do": ">5 mg/L 优良, 3~5 mg/L 偏低, <3 mg/L 危险 (缺氧)",
    "ph": "6.5~8.5 安全, 7.0~8.0 理想",
    "nh3n": "<0.5 mg/L 安全, 0.5~1.0 mg/L 偏高, >1.0 mg/L 有毒"
  }
}
```

`analyze_environment` 额外输出:
```json
{
  "trends": [{
    "sensor": "temperature",
    "current": 26.5,
    "pred_1h": 27.1,        ← 1 小时后预测值
    "trend_direction": "上升",  ← 上升/下降/平稳
    "slope_per_point": 0.032,
    "timestamp": 1750315200
  }, ...],
  "correlations": [{
    "sensor_a": "temperature",
    "sensor_b": "do",
    "pearson_r": -0.72,
    "relation": "负相关"     ← 温度↑ → 溶氧↓ (典型)
  }, ...]
}
```

---

## 五、系统提示词设计

保存在 `app_actions.c` 的 `AI_SYSTEM_PROMPT` 宏中，注入到每次请求的 `messages[0]`。

### 五大板块

| 板块 | 内容 |
|---|---|
| 四大核心能力 | 1.水质评价 2.养殖问答 3.故障诊断 4.投喂增氧方案 |
| **行为准则** (关键!) | A.水质话题必须先读传感器 B.方案建议先读+分析 C.硬件操作每次必调工具 D.先数据→再回复 |
| 水质参考区间 | 温水鱼: 温22~30 / DO>5 / pH6.5~8.5 / NH3N<0.5 |
| 投喂建议参考 | 日投喂率=饲料/鱼体重: 22~25°C→2~3%, 25~30°C→3~4%, <20°C→1~2% |
| 增氧时长建议 | DO>5→正常, DO4~5→2~4h, DO3~4→4~6h, DO<3→立即全开 |
| 板载硬件说明 | LED D7~D10 / 蜂鸣器 GPIO78 / K2 按键 GPIO28 / MMA8653 |

### 为什么行为准则 (A-D) 是必须的

Qwen2.5:7b 是一个 7B 小模型，**不会主动判断何时需要调用工具**。没有行为准则约束，模型容易：
- 凭空捏造水质数据
- 看到历史中有旧数据就不重新读取
- 认为 LED 已经亮着不需要再操作

加入准则后，模型在每次相关请求时都会**先调工具获取实时数据，再基于真实数据回复**。

---

## 六、多轮工具调用闭环

### 6.1 循环机制

```
LOOP 0: user msg → POST /api/chat
          ← assistant: {tool_calls: [read_water_quality]}
          → 执行 read_water_quality → 结果存历史 → continue

LOOP 1: (无新 user msg, 只有 tool 结果) → POST /api/chat
          ← assistant: {content: "水质评价..."}
          → 无 tool_calls, 最终回复 → break
```

上限: `TOOL_LOOP_MAX = 3` — 防止模型在 tool_calls 和补充查询之间死循环。

### 6.2 工具结果在历史中的存储

```
1. hist_add_raw(assistant_msg_json)  — 完整的 {"role":"assistant","tool_calls":[...]}
2. hist_add_tool(name, result)       — 每个工具结果 {"role":"tool","name":"...","content":"..."}
```

**Ollama 需要 `name` 字段来将 tool 结果与 assistant tool_call 关联。** 不带 `name` 时,首次工具调用可能成功,但后续调用会因上下文缺失而静默跳过。

### 6.3 为何 LED 之前"第二次就不亮了"

旧版代码只做了 `hist_add("tool", result)` — 缺少:
1. assistant `tool_calls` 消息 (模型不知道"我上次调过 control_led")
2. tool 结果的 `name` 字段 (Ollama 无法关联结果到具体 tool_call)

第二轮请求时模型看到的对话历史不完整 → 它认为"已经回复过了"或"不需要再次操作"。

---

## 七、平台适配

### 7.1 PC/Windows (`#ifndef __linux__`)

| 组件 | 行为 |
|---|---|
| `ai_agent.c` worker | 发模拟 THINKING + ANSWER 事件 (不连 Ollama) |
| `http_client.c` | 整体不编译 (空 obj) — `http_chat()` 也被 `#ifdef __linux__` 保护 |
| `ai_hardware.c` | `ai_execute_action` 返回 PC 模拟桩字符串 ("(PC模拟)") |
| 用户看到 | "这是模拟的 AI 回复。在 Linux/ARM 板卡上运行时会连接到真实的 Ollama..." |

### 7.2 ARM Linux / GEC6818 板端 (`__linux__`)

| 组件 | 行为 |
|---|---|
| `ai_agent.c` worker | 真实 HTTP POST → Ollama → tool_calls 闭环 |
| `http_client.c` | POSIX socket → `192.168.137.1:11434` (Windows 宿主) |
| `ai_hardware.c` | 真实 sysfs / GPIO / input 子系统操作硬件 |
| `edge_engine` | 从 MQTT 订阅接收数据 → 清洗 → 持久化 CSV → 维护快照+分析 |

### 7.3 默认服务器配置

```c
#ifdef __linux__
#define OLLAMA_HOST  "192.168.137.1"   // 板子 → Windows 宿主 Ollama
#else
#define OLLAMA_HOST  "localhost"        // PC 本地 (仅模拟, 不连)
#endif
#define OLLAMA_PORT  11434
#define OLLAMA_MODEL "qwen2.5:7b"
```

可通过 `ai_agent_init(host, port, model)` 在运行时覆盖。

---

## 八、UI 事件桥接

### 8.1 事件类型

| 事件 | 触发时机 | 携带数据 |
|---|---|---|
| `AI_EVT_THINKING` | 模型输出包含 `<think>...</think>` 块 | `text` (思考内容) |
| `AI_EVT_ACTION` | 工具被执行 | `tool`, `args`, `result` |
| `AI_EVT_ANSWER` | 模型最终回复 | `text` (回答) |
| `AI_EVT_ERROR` | 网络/解析/HTTP 错误 | `text` (错误描述) |
| `AI_EVT_DONE` | 一次请求完全结束 | — |

### 8.2 线程安全设计

```
Worker 线程 (pthread)           LVGL 主线程
─────────────────               ────────────
on_event() 回调被调               (空闲 → 处理 timer)
  │
  ├─ THINKING: 累积思考文本
  ├─ ACTION:   追加 "[执行 xxx] 结果" 行
  ├─ ANSWER:   保存回答文本
  ├─ ERROR:    保存错误文本
  └─ DONE:
       │
       └── lv_async_call(ai_ui_show, pkt)
                                          │
                                          ▼
                                    ai_ui_show(pkt)
                                      ├─ begin_response
                                      ├─ append_thinking
                                      ├─ finish_thinking
                                      ├─ append_answer
                                      └─ finish_response
```

**关键**: `on_event` 在 worker 线程只做 `malloc` + `strdup` 累积到 `ai_ui_packet_t`。`lv_async_call` 将包投递到 LVGL 主线程，所有 UI 函数在主线程执行。单次 `lv_async_call` 包保证 thinking → answer 的顺序不会被其他 timer 打断。

---

## 九、快捷提问设计

位于 `ai_chat_page.c` 的 `quick_msgs[]` 数组。

| # | 快捷提问 | 覆盖能力 | 期望触发的工具 |
|---|---|---|---|
| 1 | 你好，请介绍一下你自己和你的能力 | 模型问候与自我介绍 | — (问候) |
| 2 | 帮我全面评估当前水质环境状态 | **能力1**: 水质评价 | `read_water_quality` |
| 3 | 养殖罗非鱼/草鱼有哪些关键技术要点？ | **能力2**: 养殖知识问答 | (知识库, 可选工具) |
| 4 | 帮我分析系统运行状态，排查潜在故障风险 | **能力3**: 故障诊断 | `analyze_environment` |
| 5 | 根据当前环境数据，推荐最优投喂量和增氧方案 | **能力4**: 投喂增氧方案 | `read_water_quality` + `analyze_environment` |
| 6 | 帮我随机点亮一颗LED灯 | 硬件控制演示 | `random_led_on` |
| 7 | 读取加速度传感器数据，分析板子当前姿态 | 传感器读取 | `read_acceleration` |

---

## 十、如何新增一个工具

以「增加风扇控制」为例，只需改 `ai_tools.c` 一个文件:

### 步骤 1: 实现 handler

```c
static char* tool_control_fan(const cJSON *args)
{
    int speed = arg_int(args, "speed", 50);  /* 0-100 */
    /* 调用板子 PWM 控制风扇转速 (TODO: 接实际硬件) */
    LV_LOG_USER("AI: fan speed=%d%%", speed);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d}", speed);
    return strdup(buf);
}
```

### 步骤 2: 在 `g_tools[]` 加一行

```c
{
    "control_fan",
    "控制板载风扇转速 (0-100%)",
    "{\"type\":\"object\",\"properties\":{"
    "\"speed\":{\"type\":\"integer\",\"description\":\"转速百分比 0-100\"}"
    "},\"required\":[\"speed\"]}",
    tool_control_fan
},
```

### 步骤 3: 重新编译

```bash
cmake --build build
```

**模型立即就能用 `control_fan` 了** — 不需要改 Agent、不需要改 UI、不需要改系统提示词。工具 schema 由 `ai_tools_build_schema_json()` 自动注入每次请求。

---

## 十一、CMake 构建配置

### 11.1 关键变更: cJSON 解门控

`CMakeLists.txt` 中 cJSON 已从 `USE_MQTT` 块移出，**无条件编译**:

```cmake
# cJSON — always compiled (AI agent + MQTT both use it)
set(CJSON_DIR ${CMAKE_SOURCE_DIR}/src/ui-smart-water/libs/cjson)
list(APPEND MAIN_SOURCES ${CJSON_DIR}/cJSON.c)

# Paho MQTT — still gated by USE_MQTT
if(USE_MQTT)
    list(APPEND MAIN_LIBS ${PAHO_LIB_C} ${PAHO_LIB_A} pthread dl)
endif()

# Include dirs — cJSON always, Paho only when MQTT on
target_include_directories(main PRIVATE ${CJSON_DIR})
if(USE_MQTT)
    target_include_directories(main PRIVATE ${PAHO_DIR}/include)
endif()
```

### 11.2 文件自动收录

```cmake
file(GLOB_RECURSE LV_SMART_WATER_SRC src/ui-smart-water/pages/*.c)
```
`ai_agent.c`、`ai_tools.c` 等放在 `pages/ai-chat-page/` 下会自动编译，无需手动添加到 CMakeLists。

### 11.3 AI 依赖链

```
ai_agent.c → ai_tools.c → ai_hardware.c + edge_engine
     ↓            ↓
 http_client.c   cJSON
 (linux only)   (always)
```

---

## 十二、板端部署

### 12.1 编译

```bash
# 在 WSL/Linux 宿主执行交叉编译
mkdir build_arm && cd build_arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake -DUSE_MQTT=ON
make -j4
```

### 12.2 板端目录结构

```
/root/
├── aqua_data/          # 边缘引擎持久化 (CSV + 分析缓存)
│   ├── raw/            # 原始采样 CSV
│   └── analysis/       # 最新分析缓存
├── videos/             # 视频文件 (.mp4)
├── main                # LVGL 可执行文件
└── (Ollama 运行在 Windows 宿主 192.168.137.1:11434)
```

### 12.3 启动前检查

1. Windows 宿主 Ollama 监听 `0.0.0.0`: `set OLLAMA_HOST=0.0.0.0 && ollama serve`
2. 板子能 ping 通 Windows IP: `ping 192.168.137.1`
3. MQTT broker 可连 (传感器数据源)
4. 模型已拉: `ollama pull qwen2.5:7b`

---

## 十三、常见问题排查

### Q1: AI 不调用工具，直接编造数据

**原因**: 系统提示词的行为准则 (A-D) 没生效，或模型太小不遵守。

**排查**:
1. 确认 `AI_SYSTEM_PROMPT` 宏包含 `★ 关键行为准则 (必须遵守)` 段落
2. 检查 Ollama 日志，看是否真的收到了 `tools` 字段 (在请求 body 中)
3. 若模型持续不调工具，可考虑在系统提示词中加强约束，或换更大的模型 (qwen2.5:14b)

### Q2: LED 第一次能亮，第二次不亮

**已修复 (v3.0)**: `handle_tool_calls` 现在会:
1. 把完整的 assistant `tool_calls` 消息存入历史 (`hist_add_raw`)
2. 工具结果带 `name` 字段 (`hist_add_tool`)

若问题仍出现，检查:
- 对话历史是否过长被截断 (环形缓冲上限 100 轮)
- Ollama 日志中是否收到了第二轮请求的完整 history

### Q3: AI 的传感器数值与传感器页面不一致

**已修复 (v3.0)**: 所有数值都通过 `round_sensor_val()` 按 `g_sensor_phys[idx].dec` 四舍五入，与传感器页面使用相同精度。

若问题仍出现，检查:
- 两个工具读的是同一个 `edge_engine_get_latest` 吗? (是的)
- PC 模拟数据每次调用 `sim_feed` 都会变化，属于正常现象。板端 MQTT 数据不频繁变化时应当一致。

### Q4: analyze_environment 返回的数据是旧的

**已修复 (v3.0)**: `current` 字段现在从 `edge_engine_get_latest` 实时读取 (不是从分析缓存的 `regr[i].cur`)。趋势/预测/每日统计仍来自分析缓存 (这是预期行为: 它们基于历史数据计算)。

### Q5: 交叉编译时报 `cJSON.h: No such file or directory`

cJSON 现在无条件编译，include 目录无条件添加。确认 `src/ui-smart-water/libs/cjson/cJSON.h` 存在。

### Q6: 板端 Ollama 连不上

```bash
# 板端测试
curl http://192.168.137.1:11434/api/tags
# 应该返回 {"models":[{"name":"qwen2.5:7b",...}]}
```

若 `Connection refused`: Windows 端 Ollama 没有监听 `0.0.0.0`。
```powershell
set OLLAMA_HOST=0.0.0.0
ollama serve
```

---

## 附: 完整的工具调用日志示例 (板端真实运行)

```
[AI] agent ready (host=192.168.137.1, port=11434, model=qwen2.5:7b)
[AI] send '帮我全面评估当前水质环境状态'
[AI] POST to 192.168.137.1:11434 (3847 bytes)
[AI] got 1 tool_calls, dispatching (loop 1/3)
[AI] tool_call read_water_quality
[MQTT] 收到 topic=fhb/smart_aquaculture/sensor  len=167
[MQTT] 已入引擎 seq=8472 temp=26.5 humi=72.0 light=65.0 do=4.8 ph=7.18 nh3n=0.33
[AI] POST to 192.168.137.1:11434 (5210 bytes)
[AI] response: 当前水质总体健康。水温 26.5°C 处于理想区间 (22~30°C)...
```

---

**维护者**: 粤嵌园区实训 · GEC6818 + Qwen2.5 智慧水产项目  
**License**: MIT
