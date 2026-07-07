# AI Function Calling 模块 (`ai-core/`)

> 给 GEC6818 + LVGL 嵌入式项目加上**原生 Function Calling**能力的独立 C 模块。
> 模型（Qwen2.5:7b）通过 Ollama API 输出标准 `tool_calls` JSON，C 代码解析后调用注册的硬件 handler，实现"自然语言 → 控制板载硬件"的闭环。

---

## 目录

1. [这是什么](#一这是什么)
2. [解决什么问题](#二解决什么问题)
3. [架构设计](#三架构设计)
4. [文件清单](#四文件清单)
5. [快速开始（PC 测试）](#五快速开始pc-测试)
6. [核心机制](#六核心机制)
7. [API 文档](#七api-文档)
8. [如何新增一个硬件工具](#八如何新增一个硬件工具)
9. [LVGL 集成指南](#九lvgl-集成指南)
10. [交叉编译到 GEC6818](#十交叉编译到-gec6818)
11. [常见问题排查](#十一常见问题排查)
12. [设计原则与边界](#十二设计原则与边界)

---

## 一、这是什么

一个**不依赖 LVGL** 的 AI Agent C 模块，封装了：

- 与 Ollama HTTP `/api/chat` 的通信
- Qwen2.5:7b 原生 Function Calling（`tool_calls`）的解析与分发
- 工具注册表（数据驱动，新增硬件零成本）
- 事件回调机制（让 UI 层负责线程安全，自己不碰 LVGL）

跑通后，你可以对开发板说"帮我点亮 2 号 LED"，模型会输出 `control_led({"led_id":2,"state":"on"})`，C 代码自动执行硬件控制并把结果回传，模型再生成自然语言回复。

---

## 二、解决什么问题

### 原方案的痛点

原 `ai_chat_page.c` 把所有逻辑揉在一个文件里：

| 问题 | 影响 |
|---|---|
| UI、HTTP、JSON、硬件控制全耦合 | 改一个地方怕影响全部 |
| 用关键词匹配（"LED"、"蜂鸣器"）判断意图 | 模型说"把灯打开"识别不到 |
| `[ACTION]` 标签嵌入模型输出 | 模型经常格式不规范，解析失败 |
| AI 逻辑直接调 `lv_async_call` | 无法在 PC 上脱离 LVGL 测试 |

### 新方案怎么解决

| 痛点 | 新方案 |
|---|---|
| 耦合 | 分三层：UI / Agent / Tools，独立编译 |
| 关键词匹配 | 用 Qwen2.5 原生 `tool_calls`，模型理解语义 |
| `[ACTION]` 标签 | 删除，改用标准 JSON Schema |
| 不能 PC 测试 | Agent + Tools 不依赖 LVGL，PC 上 `make` 直接跑 |

---

## 三、架构设计

```
┌─────────────────────────────────────────────────────────────┐
│ GEC6818 应用进程 (可整体移植)                                │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ UI 层 (依赖 LVGL)                                    │   │
│  │   ai_chat_page.c  ←→  ai_ui_adapter.c               │   │
│  │   只管显示和输入 / 通过 adapter 调 agent              │   │
│  └─────────────────────────────────────────────────────┘   │
│                       ↓ 事件回调   ↑ ai_agent_send()        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ AI Agent 层 (独立模块 · 不依赖 LVGL) ★              │   │
│  │   ai_agent.c — HTTP / tool_calls 解析 / 事件回调     │   │
│  └─────────────────────────────────────────────────────┘   │
│                       ↓ tool_calls   ↑ 工具结果             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ 工具层 (独立模块 · 硬件相关) ★                       │   │
│  │   ai_tools.c — 工具注册表 / LED / 蜂鸣器 / 传感器    │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
        ↓                                    ↓
┌──────────────────┐                ┌─────────────────────┐
│ GEC6818 硬件      │                │ Ollama Server (PC)  │
│ /sys/class/leds   │                │ qwen2.5:7b          │
│ /dev/*  GPIO/I2C │                │ HTTP :11434         │
└──────────────────┘                └─────────────────────┘
```

**关键设计**：`★` 标记的两层不依赖 LVGL，可以单独 `gcc` 编译，PC 上跑测试，调通了再交叉编译塞到板子上。

---

## 四、文件清单

| 文件 | 行数 | 职责 |
|---|---|---|
| `ai_agent.h` | ~60 | Agent 对外接口：事件类型、回调、公开 API |
| `ai_agent.c` | ~250 | Agent 实现：HTTP 通信 + tool_calls 解析循环 + 事件回调 |
| `ai_tools.h` | ~40 | 工具注册表接口：handler 类型、注册项、dispatch |
| `ai_tools.c` | ~170 | 工具注册表实现 + 硬件控制（PC mock / 板子真实双版本） |
| `ai_ui_adapter.c` | ~110 | LVGL 适配层：事件 → `lv_async_call` → UI 函数 |
| `http_client.h` | ~25 | HTTP 客户端接口 |
| `http_client.c` | ~140 | POSIX socket HTTP/1.1 实现（WSL2/板子通用） |
| `main_test.c` | ~90 | PC 测试入口：交互式终端 + printf 验证 |
| `Makefile` | ~70 | 三种编译目标：pc_test / arm / get_cjson |

---

## 五、快速开始（PC 测试）

### 环境准备

```bash
# WSL2 Ubuntu
sudo apt install build-essential

cd ai-core
make get_cjson        # 下载 cJSON 单文件库
```

### Windows 端启动 Ollama（关键！）

WSL2 是独立网络命名空间，必须让 Ollama 监听 `0.0.0.0`：

```powershell
# Windows PowerShell
set OLLAMA_HOST=0.0.0.0
ollama serve
```

### 拿到 Windows IP

```bash
# WSL2 里执行
cat /etc/resolv.conf | grep nameserver | awk '{print $2}'
# 输出类似 172.28.80.1
```

把这个 IP 改到 `main_test.c` 顶部：

```c
const char *host = "172.28.80.1";  // 改成你的实际 Windows IP
```

### 编译并运行

```bash
make            # = make pc_test
make run        # 编译并直接跑
```

### 测试用例

```
用户> 你好                              # 普通对话, 不该调工具
用户> 帮我点亮 1 号 LED                 # 触发 control_led
用户> 把所有 LED 关掉                   # 多次调用 + 参数推理
用户> 蜂鸣器响 1 秒                     # 触发 control_buzzer
用户> 现在温度多少                      # 触发 read_temperature
用户> /clear                            # 清空对话历史
用户> /quit                             # 退出
```

### 验证 FC 工作正常的标志

```
[工具调用] control_led({"led_id":1,"state":"on"})
  └─ 结果: {"ok":true,"led":1,"state":"on"}
[MOCK hw_led_set] LED 1 -> ON
[最终回答] 已为你点亮 1 号 LED
```

看到 **`[工具调用]`** + **`[MOCK hw_led_set]`** + **`[最终回答]`** 三段都出现，说明 FC 闭环完全跑通。

---

## 六、核心机制

### 1. Function Calling 闭环（最重要）

```
用户输入 ──→ ai_agent_send()
              │
              ▼
       POST /api/chat (带 tools schema)
              │
              ▼
       模型返回 response
              │
       ┌──────┴──────┐
       ▼             ▼
   有 tool_calls   无 tool_calls
       │             │
       ▼             ▼
   执行硬件 handler  content 即最终回复
       │             │
       ▼             ▼
   结果作为 role=tool  触发 AI_EVT_ANSWER
   追加历史
       │
       ▼
   回到 POST /api/chat
   (最多循环 3 次, 防止死循环)
```

代码位置：`ai_agent.c` 的 `worker_thread()` 函数。

### 2. 工具注册表（数据驱动）

新增一个硬件工具**只需要两步**：

1. 写 handler 函数
2. 在 `g_tools[]` 加一行

```c
// ai_tools.c
static char* tool_control_led(const cJSON *args) {
    int led_id = cJSON_GetObjectItem(args, "led_id")->valueint;
    /* ... */
}

static const ai_tool_t g_tools[] = {
    {"control_led", "控制 LED", "{schema}", tool_control_led},
    // ↑ 加一行就完成新工具注册
};
```

模型通过自动生成的 `tools` JSON Schema 知道有哪些工具可用。

### 3. 事件回调（解耦 LVGL）

Agent 不直接调用任何 LVGL 函数，而是发事件：

| 事件类型 | 触发时机 | 字段 |
|---|---|---|
| `AI_EVT_THINKING` | 模型输出思考块 | `text` |
| `AI_EVT_ACTION` | 工具被调用 | `tool`, `args`, `result` |
| `AI_EVT_ANSWER` | 模型最终回复 | `text` |
| `AI_EVT_ERROR` | 出错 | `text` |
| `AI_EVT_DONE` | 一次请求完全结束 | (无) |

UI 层（adapter）注册一个回调，在回调里用 `lv_async_call` 把事件转到主线程。

### 4. PC/板子双版本硬件控制

`ai_tools.c` 用 `#ifdef __PC_TEST__` 切换：

```c
static int hw_led_set(int led_id, int on) {
#ifdef __PC_TEST__
    printf("  [MOCK] LED %d -> %s\n", led_id, on ? "ON" : "OFF");
    return 0;
#else
    /* 真实 sysfs / 寄存器操作 */
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/led%d/brightness", led_id);
    /* ... */
#endif
}
```

- PC 测试：`make` 默认带 `-D__PC_TEST__`
- 板子部署：`make arm` 不带这个宏，走真实硬件分支

---

## 七、API 文档

### `ai_agent.h`

```c
/* 初始化 Agent */
void ai_agent_init(const char *host, int port, const char *model);

/* 注册事件回调 (worker 线程触发, UI 层负责线程安全) */
void ai_agent_set_callback(ai_event_cb_t cb, void *user_data);

/* 设置系统提示词 (模型人设 / 能力说明) */
void ai_agent_set_system(const char *system_prompt);

/* 异步发送用户消息 (内部起线程, 立即返回) */
bool ai_agent_send(const char *user_msg);

/* 中断当前请求 */
void ai_agent_stop(void);

/* 清空对话历史 */
void ai_agent_clear_history(void);

/* 查询是否正在处理 */
bool ai_agent_is_running(void);
```

### `ai_tools.h`

```c
/* 工具 handler 函数签名: 接收 args, 返回 malloc'd 结果字符串 */
typedef char* (*ai_tool_handler_t)(const cJSON *args);

/* 获取工具注册表 */
const ai_tool_t* ai_tools_get_all(int *count);

/* 生成 Ollama tools 字段 JSON (malloc'd) */
char* ai_tools_build_schema_json(void);

/* 按 name 派发到对应 handler (malloc'd 结果, 调用方释放) */
char* ai_tools_dispatch(const char *name, const cJSON *args);
```

### `http_client.h`

```c
/* 同步 POST, 阻塞 timeout_s 秒. 调用方负责 http_response_free */
HttpResponse* http_post(const char *host, int port,
                        const char *path, const char *body,
                        int timeout_s);

void http_response_free(HttpResponse *r);
```

---

## 八、如何新增一个硬件工具

以"控制风扇"为例：

### 步骤 1：实现 handler

在 `ai_tools.c` 加：

```c
static char* tool_control_fan(const cJSON *args) {
    int speed = cJSON_GetObjectItem(args, "speed")->valueint;  /* 0-100 */

#ifdef __PC_TEST__
    printf("  [MOCK hw_fan_set] speed=%d%%\n", speed);
#else
    /* TODO: 接 GEC6818 PWM, 写 /sys/class/pwm/... */
#endif

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d}", speed);
    return strdup(buf);
}
```

### 步骤 2：注册到 `g_tools[]`

```c
static const ai_tool_t g_tools[] = {
    /* ... 已有的工具 ... */
    {
        "control_fan",                              /* name (模型可见) */
        "控制风扇转速",                              /* description */
        "{\"type\":\"object\",\"properties\":{"     /* JSON Schema */
        "\"speed\":{\"type\":\"integer\","
        "\"description\":\"转速百分比 0-100\"}"
        "},\"required\":[\"speed\"]}",
        tool_control_fan                             /* handler */
    },
};
```

### 步骤 3：重新编译

```bash
make clean && make
```

模型立即就能用 `control_fan` 了，**不需要改 agent 一行代码**。

---

## 九、LVGL 集成指南

### 1. 工程链接

把 `ai-core/` 拷到你的 LVGL 工程目录，在你的 Makefile 加：

```makefile
CFLAGS  += -I./ai-core
LDFLAGS += -L./ai-core -lai_core -lpthread
# 把 ai-core/cJSON.c 加到工程源文件列表
```

### 2. 初始化（在 app_main 或 screen 创建时）

```c
#include "ai_agent.h"
#include "ai_ui_adapter.h"
#include "ai_chat_page.h"   /* 你已有的 UI API */

void app_main(void) {
    lv_init();
    /* ... LVGL 显示初始化 ... */

    lv_obj_t *screen = ai_chat_page_create();

    ai_agent_init("192.168.137.1", 11434, "qwen2.5:7b");
    ai_agent_set_callback(ai_ui_adapter_get_cb(), NULL);
    ai_agent_set_system("你是智慧水产养殖AI助手。");
    ai_ui_adapter_init(screen);
}
```

### 3. 发送按钮回调（替换原来的接收线程）

```c
static void send_btn_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_user_data(e);
    const char *text = lv_textarea_get_text(ta);
    if (!text || !text[0]) return;

    ai_chat_page_begin_response(g_screen);    /* UI 显示 loading */
    ai_agent_send(text);                       /* 异步发送 */
    lv_textarea_set_text(ta, "");               /* 清空输入框 */
}
```

### 4. 停止 / 清空按钮

```c
static void stop_btn_cb(lv_event_t *e)  { ai_agent_stop(); }

static void clear_btn_cb(lv_event_t *e) {
    ai_agent_clear_history();
    ai_chat_page_clear_messages(g_screen);
}
```

### 5. UI 更新全在 adapter 里

`ai_ui_adapter.c` 已经把 5 种事件桥接到 `lv_async_call`，自动调用你已有的 `ai_chat_page_append_thinking()` / `ai_chat_page_append_answer()` 等 UI 函数。**原有的 UI 函数完全不用改**。

---

## 十、交叉编译到 GEC6818

### 1. 生成 ARM 静态库

```bash
cd ai-core
make arm       # 生成 ai_core.a
```

### 2. 链接到 LVGL 工程

```makefile
# 你的 LVGL 工程 Makefile
CC = arm-linux-gcc
CFLAGS  += -I./ai-core
LDFLAGS += -L./ai-core -lai_core -lpthread

# 把 ai-core/cJSON.c 加到源文件列表
SRCS += ai-core/cJSON.c
```

### 3. 把真实硬件代码写实

把 `ai_tools.c` 里 `#else` 分支的 TODO 替换成实际硬件操作：

```c
static int hw_led_set(int led_id, int on) {
#ifdef __PC_TEST__
    printf("  [MOCK] LED %d -> %s\n", led_id, on ? "ON" : "OFF");
    return 0;
#else
    /* 真实硬件: 写 sysfs 或 mmap 寄存器 */
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/led%d/brightness", led_id);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d", on ? 1 : 0);
    fclose(f);
    return 0;
#endif
}
```

---

## 十一、常见问题排查

### Q1: `connect failed: Connection refused`

**原因**：Windows Ollama 没监听 `0.0.0.0`，只监听 `127.0.0.1`。

**解决**：
```powershell
set OLLAMA_HOST=0.0.0.0
ollama serve
```

### Q2: `JSON parse failed`

**原因**：模型返回的不是合法 JSON。

**排查**：在 `ai_agent.c` 的 `worker_thread` 里加 `printf` 打印 `resp`，看模型实际返回了什么。常见是上下文超长被截断。

### Q3: 没出现 `[工具调用]`，只有 `[最终回答]`

**原因**：`tools` 字段没传成功，或模型没识别为工具调用。

**排查**：在 `build_body()` 里打印 `tools_json`，确认 schema 没问题。也可以用 curl 手动测一次：

```bash
curl http://YOUR_HOST:11434/api/chat -d '{
  "model": "qwen2.5:7b",
  "messages": [{"role":"user","content":"点亮LED"}],
  "tools": [{"type":"function","function":{"name":"control_led","parameters":{...}}}],
  "stream": false
}'
```

### Q4: 模型偶尔回英文

**解决**：在 system prompt 加 `"始终用简体中文回复。"`。

### Q5: `cJSON.h: No such file or directory`

**解决**：`make get_cjson` 重新下载。

### Q6: 一次输入触发多次 `[工具调用]`

**正常**：这是 FC 的多轮调用。模型可能先调 `read_temperature` 拿数据，再调 `control_led` 根据温度调灯光。Agent 内部最多循环 3 次（`TOOL_LOOP_MAX`），防止死循环。

### Q7: 思考块（`）干扰输出

**已处理**：`ai_agent.c` 的 `emit_thinking_if_any()` 会自动拆分，思考内容走 `AI_EVT_THINKING` 事件，最终回复走 `AI_EVT_ANSWER`。

---

## 十二、设计原则与边界

### 设计原则

1. **解耦**：AI Agent 和 Tools 不依赖 LVGL，可独立编译测试
2. **数据驱动**：新增硬件只改注册表，不改 agent 逻辑
3. **事件驱动**：UI 通过回调接收事件，agent 不主动碰 UI
4. **双版本**：PC mock 和板子真实硬件用条件编译切换
5. **闭环**：tool_calls 结果回传模型，模型基于真实数据生成回复

### 不做的事（边界）

- ❌ 不实现流式响应（stream:true）—— 用 `stream:false` 简化解析
- ❌ 不解析 HTTP chunked transfer-encoding —— Ollama 在 stream:false 下不用
- ❌ 不依赖第三方 JSON 库 —— 用 cJSON 单文件库
- ❌ 不处理多模态（图片/音频）—— 只做文本 + tool_calls
- ❌ 不实现 RAG / 向量检索 —— 工具结果就是模型的"记忆"

### 性能参考（GEC6818, 1GB RAM）

| 指标 | 数值 |
|---|---|
| 单次请求耗时 | 2-8 秒（取决于模型负载和 prompt 长度） |
| 内存占用 | ~5MB（agent + cJSON + HTTP buffer） |
| 工具调用循环上限 | 3 次（`TOOL_LOOP_MAX`） |
| 对话历史上限 | 100 轮（环形缓冲） |

---

## 附：完整文件树

```
ai-core/
├── README.md            ← 本文档
├── Makefile             ← 编译脚本
├── ai_agent.h           ← Agent 接口
├── ai_agent.c           ← Agent 实现
├── ai_tools.h           ← 工具注册表接口
├── ai_tools.c           ← 工具注册表 + 硬件控制
├── ai_ui_adapter.c      ← LVGL 适配层
├── http_client.h        ← HTTP 客户端接口
├── http_client.c        ← POSIX socket HTTP 实现
├── main_test.c          ← PC 测试入口
├── cJSON.c              ← (make get_cjson 下载)
└── cJSON.h              ← (make get_cjson 下载)
```

---

**维护者**：粤嵌园区实训 · GEC6818 + Qwen2.5 项目
**最后更新**：2026-07-06
**License**：MIT
