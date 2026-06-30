# AI 硬件控制 — 完整开发日志

> **让一个大语言模型（DeepSeek-R1:7B）通过提示词工程控制真实嵌入式硬件**
>
> 目标平台：GEC6818 ARM Linux 板卡 (S5P6818)
> 硬件外设：4路 LED (D7~D10)、蜂鸣器 (PWM2)、按键 (K2)、MMA8653FCR1 三轴加速度计
> 模型：DeepSeek-R1:7B via Ollama (HTTP POST → `/api/chat`, stream:false)

---

## 目录

1. [硬件配置](#1-硬件配置)
2. [方案选型：为什么不直接用 Function Calling](#2-方案选型为什么不直接用-function-calling)
3. [架构演变 — 三代设计方案](#3-架构演变--三代设计方案)
4. [Bug 全记录（14 个）](#4-bug-全记录14-个)
5. [当前代码框架](#5-当前代码框架)
6. [深度思考：这算真正的 AI 控制硬件吗？](#6-深度思考这算真正的-ai-控制硬件吗)
7. [与 MCP / OpenAI Function Calling 的对比](#7-与-mcp--openai-function-calling-的对比)
8. [经验教训](#8-经验教训)
9. [未来升级路线](#9-未来升级路线)

---

## 1. 硬件配置

### 1.1 GEC6818 开发板外设

| 元件 | 丝印 | 芯片引脚 | Linux 接口 | GPIO 编号 | 有效电平 |
|------|------|---------|-----------|-----------|---------|
| LED-1 | D7 | B26 | `/sys/class/leds/led1/brightness` | 58 | 1=亮 0=灭 |
| LED-2 | D8 | C11 | `/sys/class/leds/led2/brightness` | 75 | 1=亮 0=灭 |
| LED-3 | D9 | C7 | `/sys/class/leds/led3/brightness` | 71 | 1=亮 0=灭 |
| LED-4 | D10 | C12 | `/sys/class/leds/led4/brightness` | 76 | 1=亮 0=灭 |
| 按键 | K2 | A28 | GPIO sysfs (gpio28) | 28 | 0=按下 1=释放 |
| 蜂鸣器 | — | GPIOC14 | GPIO sysfs (gpio78) | 78 | 1=鸣响 0=静音 |
| 加速度计 | MMA8653 | I2C-2, 0x1D | `/dev/input/event3` (input子系统) | — | — |

### 1.2 GPIO 编号公式 (S5P6818 / Nexell)

```
GPIOA = 0    GPIOB = 32   GPIOC = 64
GPIOD = 96   GPIOE = 128  GPIOF = 160
```

已通过 `/sys/kernel/debug/gpio` 和 `gpiochip` 验证。

### 1.3 LED 子系统 vs 裸 GPIO

GEC6818 内核的 `nxp-gpio` LED 驱动接管了 D7~D10 对应的 GPIO 引脚。`/sys/class/gpio/gpio{58,75,71,76}/` 被内核标记为 "used"，用户空间直接操作 GPIO value 会被内核拒绝。必须通过 `/sys/class/leds/ledX/brightness` 操作。

**关键发现**：LED 子系统 `brightness` 接口的约定是 `1=ON(最大亮度), 0=OFF`，内核驱动内部处理硬件极性（共阳极 active-low）的转换。用户空间只需要写 `1`/`0` 即可，不需要关心硬件极性。

### 1.4 MMA8653FCR1 加速度计数据解读

**硬件层**：
- 芯片：NXP MMA8653FCR1
- 总线：I2C-2 (MCU_SCL_2 / MCU_SDA_2)，地址 0x1D
- 内核驱动：`mma8653` → 注册为 `/dev/input/event3`，`name="mma8653"`
- 内核驱动已绑定 I2C 地址（`i2cdetect` 显示 UU），用户空间无法通过 `/dev/i2c-2` 裸访问

**数据格式**：
- ±2g 量程，10-bit 模式
- 原始ADC范围：~[-512, 511]（10-bit 有符号）
- 换算公式：`g值 = 原始值 / 256.0`
- **重要**：`EVIOCGABS` ioctl 返回的 `min=-32 / max=31` 是内核驱动的假象，忽略！

**板卡平放实测**（通过 `dd if=/dev/input/event3 bs=24 | od -A x -t d4` 协议验证）：

| 轴 | 原始值 | 物理量 | 含义 |
|----|--------|--------|------|
| X | 10~14 | ~0.05g | 基本水平，轻微右倾 |
| Y | 1~4 | ~0.01g | 基本水平 |
| Z | -258~-265 | ~-1.02g | **地球重力！** 板子平放，Z轴朝下承受1g |

**`input_event` 协议解码** (32-bit ARM, 16字节/帧)：

```
int32[0]: tv_sec    (时间戳秒)
int32[1]: tv_usec   (时间戳微秒)
int32[2]: type+code (低16位=type, 高16位=code)
int32[3]: value     (数值)

type=3 (EV_ABS):  code=0→X轴  code=1→Y轴  code=2→Z轴
type=0 (EV_SYN):  同步分隔符, code=0
```

**Shell 验证命令**：
```bash
# 方法1：读raw事件
dd if=/dev/input/event3 bs=24 count=20 2>/dev/null | od -A x -t d4

# 方法2：ioctl直接读（和C代码相同）
cat > /tmp/accel.c << 'END'
#include <stdio.h>
#include <fcntl.h>
#include <linux/input.h>
int main(void){
  int fd=open("/dev/input/event3",0);
  struct input_absinfo x={0},y={0},z={0};
  ioctl(fd,EVIOCGABS(0x00),&x);
  ioctl(fd,EVIOCGABS(0x01),&y);
  ioctl(fd,EVIOCGABS(0x02),&z);
  printf("X: val=%d min=%d max=%d\n",x.value,x.minimum,x.maximum);
  printf("Y: val=%d min=%d max=%d\n",y.value,y.minimum,y.maximum);
  printf("Z: val=%d min=%d max=%d\n",z.value,z.minimum,z.maximum);
  close(fd); return 0;
}
END
gcc -o /tmp/accel /tmp/accel.c && /tmp/accel
```

---

## 2. 方案选型：为什么不直接用 Function Calling

### 2.1 可选方案

| 方案 | 描述 | 结论 |
|------|------|------|
| **A. 原生 Function Calling** | Ollama `/api/chat` 的 `tools` 参数，依赖模型原生 `tool_calls` 输出 | ❌ 官方 `deepseek-r1:7b` **不支持** |
| **A2. MFDoom/deepseek-r1-tool-calling** | 社区修改版，添加 tool calling 支持 | ⚠️ Ollama 官方标注为**不稳定**（循环调用、空响应）|
| **A3. 换模型 (Qwen3 / Llama3)** | 换用原生支持 function calling 的模型 | ⚠️ 需要额外 8-14GB 磁盘空间，开发板存储有限 |
| **B. 提示词工程 + 标签解析** | System Prompt 注入工具描述 → AI 输出特定标记 → C 代码解析并执行 | ✅ **选用** |

### 2.2 方案 B 初版设计

核心理念：让 AI 在回复中输出结构化的命令标签，C 代码扫描标签、执行硬件操作、从回复中移除标签后展示给用户。

```
AI 回复示例：
  <action>led_on</action>
  <think>用户需要查看养殖环境，先开灯再读取传感器数据</think>
  好的，已经打开照明灯。当前水温 25.3°C，pH 值 7.2。
```

C 代码处理管道：
```
1. ai_split_think(content)   → 提取 <think>...</think> 到思考面板
2. ai_split_actions(content) → 扫描 <action>cmd</action> → 执行 → 移除标签
3. 干净的回答文本 → 显示给用户
```

### 2.3 为什么方案 B 在实践中遇到了严重问题

DeepSeek-R1:7B 是一个推理模型（擅长 `<think>` 思考链），但**不擅长结构化输出**。在实践过程中暴露出以下模型层面的问题：

1. **命令名幻觉**：AI 输出不存在的命令（`ledX_on`、`led8_on`）
2. **XML 格式混淆**：AI 将 `<action>` 误解为 XML 元素，在内部填入推理文本
3. **命令后附加中文**：如 `led_random_on就可以了。` — 命令名被污染
4. **传感器/硬件混淆**：用户问 LED 控制，AI 却输出 `read_accel`
5. **上下文污染导致雪崩**：一旦历史中出现传感器 debug 数据，后续回合 AI 完全混乱

这些问题最终导致了架构从 **AI 驱动** 转向 **C 代码预检测 + AI 解读** 的混合模式。

---

## 3. 架构演变 — 三代设计方案

### 3.1 v1 — XML `<action>` 标签 (初期 — 已被废弃)

```
用户消息 → Ollama → AI 输出 <action>cmd</action> → C 解析 → 执行硬件 → 显示
```

**格式**：`<action>led_on</action>`（XML 风格标签）

**问题**：DeepSeek-R1 将 `<action>` 误解为 XML 元素。AI 会在标签内部填入推理文本：
```
<action>用户需要打开LED，我应该执行led_on命令...led_on</action>
```
导致命令提取提取到的是推理文本而非命令名。

**结论**：XML 格式对 DeepSeek-R1 有语义干扰，需要换成非 XML 标记。

---

### 3.2 v2 — `[ACTION]` 非 XML 标记 (中期 — 仍不可靠)

```
用户消息 → Ollama → AI 输出 [ACTION]命令名 → C 解析 → 执行硬件 → 显示
```

**格式**：`[ACTION]led_random_on`（方括号标记，非 XML）

**改进**：避免了 XML 语义干扰。但暴露了新问题：

1. **AI 仍在幻觉**：输出 `[ACTION]ledX_on`、`[ACTION]led8_on`
2. **传感器/硬件混淆**：用户 "开LED" → AI 输出 `[ACTION]read_accel`
3. **多次执行**：`ai_split_actions` 被调用 3 次（answer、think、history），每次执行一次硬件。`led_random_on` 每次随机选不同 LED → "开一盏灯却亮了两三盏"
4. **格式仍然不稳定**：AI 会输出 `[ACTION]led_random_on就可以了。`

**结论**：7B 模型在结构化命令输出方面根本不可靠。需要更激进的架构变更。

---

### 3.3 v3 — C 代码意图预检测 + AI 解读 (当前架构)

```
用户消息 → C 代码 strstr 关键词匹配 → 执行硬件 → 结果注入消息
        → Ollama → AI 看到 "(系统已执行: ...)" → 自然回复
```

**核心变更**：硬件操作**不再依赖 AI 输出格式化命令**。C 代码在消息发给 AI **之前**，用 `strstr` 关键词匹配检测用户意图，直接执行硬件，把结果追加到用户消息末尾。AI 只需要看到执行结果并自然回复。

**意图检测表**：

| 用户消息包含 | C 代码执行 | 注入格式 |
|------------|-----------|---------|
| "随机" + "LED"/"灯" | `led_random_on` | `(系统已执行: [LED] ...)` |
| "切换蜂鸣器"/"蜂鸣器开关" | `buzzer_toggle` | `(系统已执行: [蜂鸣器] ...)` |
| "加速度"/"偏转角"/"姿态" | `read_accel` | `(系统已执行: [加速度] X=... Y=... Z=...)` |
| "按键"/"按钮" | `read_button` | `(系统已执行: [按键-K2] ...)` |

**System Prompt (精简版)**：
```
你是智慧水产养殖AI助手，精通水质管理、鱼病诊断、投喂策略、养殖技术。
硬件操作(开灯/蜂鸣器/传感器)由系统自动执行，
用户消息中「(系统已执行: ...)」表示操作已完成，
你只需根据结果自然回复，无需重复操作。
```

**关键设计决策 — 不提及任何命令语法**：v1/v2 的 System Prompt 包含了详细的命令列表和格式说明。这些信息对 DeepSeek-R1 构成了**语义干扰** — 模型看到 `[ACTION]` 格式后会倾向于模仿输出，即使不需要。v3 的 System Prompt 完全不提及任何命令语法。

**`[ACTION]` 解析器保留为隐藏通道**：C 代码中的 `ai_split_actions()` 和 `ai_strip_actions()` 仍然保留。如果 AI 碰巧输出了正确格式的 `[ACTION]` 命令，仍然会被执行。但它不再在 System Prompt 中被教导给 AI。

**处理管道 (v3 完整流程)**：

```
ai_recv_thread():
│
├─ 1. 意图预检测 (app_actions.c:1260-1279)
│     strstr(user_msg, "随机") && strstr(user_msg, "LED")
│     → ai_execute_action("led_random_on")
│     → 结果注入用户消息: "xxx\n(系统已执行: [LED] ...)"
│
├─ 2. ai_build_body(aug_msg) → 构建 JSON 请求体
│     ai_hist_add("user", aug_msg)  ← 先存入历史
│     → 构建: {model, messages:[system, ...history], stream:false}
│
├─ 3. HTTP POST → Ollama /api/chat
│
├─ 4. ai_json_val(response, "content") → 提取 AI 回复
│
├─ 5. ai_split_think(content) → 提取 <think> 到思考面板
│     content 被原地修改，移除 <think> 块
│
├─ 6. ai_split_actions(content) → 扫描 [ACTION] 并执行
│     (通常为空 — AI 不应再输出 [ACTION])
│
├─ 7. ai_strip_actions(thinking) → 从 think 中移除 [ACTION]
│     (仅移除，不执行 — AI 可能在 think 中引用语法)
│
├─ 8. 前导空白裁剪 → 剩余 content 即回答
│
├─ 9. 传感器结果注入历史 (仅传感器，不注入 LED/蜂鸣器)
│     strstr(action_results, "[加速度") → ai_hist_add("user", ...)
│
├─ 10. AI 回复存入历史
│      ai_hist_add("assistant", stripped_content)
│
└─ 11. lv_async_call(ai_ui_show) → 单次回调保证显示顺序
```

---

## 4. Bug 全记录（14 个）

### Bug #1: LED 极性反转
- **症状**：代码写 `'0'` 想开灯，灯不亮
- **根因**：代码假设 GPIO 是 active-low（0=亮），但 LED 子系统 `brightness` 接口是 `1=ON, 0=OFF`。内核驱动内部处理极性转换，用户空间只需要写 1/0
- **修复**：全局替换 `ledX_on` → 写 `'1'`，`ledX_off` → 写 `'0'`
- **验证**：`echo 1 > /sys/class/leds/led2/brightness` 灯亮，确认 1=ON
- **位置**：`ai_hardware.c` LED 控制段

---

### Bug #2: 字符串截断 — 内容被意外抹掉
- **症状**：屏幕中间 AI 回答区域大部分内容显示不出来
- **根因**：`ai_split_actions()` 和 `ai_split_think()` 中，`memmove` 在 `break` 分支里已复制了 NUL 终止符，但循环结束后又执行了 `if (dst != text) *dst = '\0'`，把最后一个有效字符误抹为 NUL
- **分析**：
  ```c
  while (*src) {
      if (!tag) {
          memmove(dst, src, rest + 1);  // 这里已经复制了 NUL
          break;
      }
      // ...
  }
  if (dst != text) *dst = '\0';  // ← 这行又抹了一个字节！
  ```
- **修复**：引入 `bool hit_break` 标志，只有循环正常结束时才补 NUL：
  ```c
  if (!hit_break && dst != text)
      *dst = '\0';
  ```
- **位置**：`ai_hardware.c` 的 `ai_split_actions()` 和 `ai_split_think()`

---

### Bug #3: 多次执行 — 一次请求执行 N 次硬件
- **症状**：点 "随机开一个 LED" → 有时亮两三个灯
- **根因**：`ai_split_actions()` 被调用 3 次 — 分别在 answer、think、history 三个文本上。每次调用都执行硬件。`led_random_on` 每次随机选不同的 LED
- **修复**：创建 `ai_strip_actions()` — 只移除 `[ACTION]` 标记，不执行硬件。用于 think 和 history。只有 answer 内容才执行
- **位置**：`ai_hardware.c` 新增 `ai_strip_actions()`；`app_actions.c` 调用方

---

### Bug #4: DeepSeek-R1 XML 格式混淆
- **症状**：AI 将 `<action>cmd</action>` 当作 XML 元素，在标签内部填入大量推理文本
- **根因**：DeepSeek-R1 的训练数据包含 XML，模型有很强的 XML schema 先验
- **修复**：将语法从 `<action>cmd</action>` (XML) 改为 `[ACTION]cmd` (非 XML 标记)
- **位置**：全局语法迁移

---

### Bug #5: AI 命令名幻觉
- **症状**：AI 输出 `[ACTION]ledX_on`、`[ACTION]led8_on`、`[ACTION]read_accel`（用户明明问的是 LED）
- **根因**：DeepSeek-R1:7B 在结构化输出方面不可靠。模型将 "LED" 泛化为 "ledX"，或混淆传感器读取与硬件控制
- **修复**：这是 **架构级问题**，不是代码 bug。最终通过 v3 架构（C 代码意图预检测）绕过 — 不再依赖 AI 输出格式化命令
- **位置**：架构决策，影响 `app_actions.c` 和 System Prompt

---

### Bug #6: 命令名后附加中文
- **症状**：AI 输出 `[ACTION]led_random_on就可以了。`
- **根因**：DeepSeek-R1 在命令后面自然地续写了中文解释
- **修复**：命令提取只接受 `[a-zA-Z0-9_]` 字符，第一个非匹配字节即截断：
  ```c
  while (*ae && ((*ae >= 'a' && *ae <= 'z')
              || (*ae >= 'A' && *ae <= 'Z')
              || (*ae >= '0' && *ae <= '9')
              || *ae == '_'))
      ae++;
  *ae = '\0';
  ```
- **位置**：`ai_hardware.c` 的 `ai_split_actions()` 命令解析段

---

### Bug #7: GPIO 方向切换 bug — 蜂鸣器 Toggle 永远不反转
- **症状**：蜂鸣器只会响，不会停
- **根因**：
  ```
  buzzer_toggle 流程:
  1. gpio_read(78) → gpio_ensure(78, "in")  ← 输出驱动被关闭！
  2. 引脚切为输入 → 电平由电路决定 → 读回值不可靠
  3. 假设正在响(output=1) → 切输入后读到 '0' → flip → set='1'
  4. gpio_write(78, '1') → 继续响 ← 永远不停！
  ```
- **修复**：用 `static char buzzer_state` 软件变量跟踪最后一次写入的状态，不再读 GPIO：
  ```c
  static char buzzer_state = '0';
  buzzer_toggle: set = (buzzer_state=='1') ? '0' : '1';
                 gpio_write(78, set); buzzer_state = set;
  ```
- **同步修复**：`gpio_ensure()` 在 GPIO 已导出时也强制重设方向（之前跳过，导致 gpio_read 后 gpio_write 无法切回输出）
- **位置**：`ai_hardware.c` 的 `buzzer_toggle` 和 `gpio_ensure()`

---

### Bug #8: 传感器数据污染 AI 上下文（核心架构 bug）
- **症状**：先问传感器 → 再问 LED → AI 回答完全混乱
- **根因**：`accel_read()` 返回 ~400 字节的调试 dump：
  ```
  [加速度-MMA8653] ioctl 成功
    X: value=-5      min=-32     max=31     fuzz=0  flat=0  res=0
    Y: value=-259    min=-32     max=31     fuzz=0  flat=0  res=0
    ...
    量程推测: X[-32,31] Y[-32,31] Z[-32,31]
  ```
  这个字符串拼接进用户消息 → 存入对话历史 → 后续所有回合的 AI 上下文都被污染。`[加速度-MMA8653]` 与 System Prompt 中的 `[ACTION]` 格式高度相似，DeepSeek-R1 产生格式混淆。
- **修复**：`accel_read()` 返回紧凑一行：
  ```
  [加速度] X=12(~0.05g) Y=3(~0.01g) Z=-261(~-1.02g) — 10-bit原始值 ±2g量程
  ```
  调试细节通过 `LV_LOG_USER` 打印到串口，不进入 AI 上下文
- **配套修复**：System Prompt 不再提及 `[ACTION]` 语法（减少格式干扰）
- **位置**：`ai_hardware.c` 的 `accel_read()` 和 `app_actions.c` 的 `OLLAMA_SYSTEM_MSG`

---

### Bug #9: ioctl min/max 误导 — 加速度计量程误判
- **症状**：`EVIOCGABS` 返回 `min=-32, max=31`，但实际数据 Y=-259 远超此范围
- **根因**：ioctl 返回的 min/max 是内核驱动报告的值，不代表芯片的真实量程。MMA8653 配置为 ±2g 模式，10-bit ADC 原始值范围是 [-512, 511]
- **修复**：代码注释和输出格式中标注真实量程 `±2g, 10-bit原始值`，忽略 ioctl 的 min/max
- **验证方法**：`dd if=/dev/input/event3 bs=24 count=20 2>/dev/null | od -A x -t d4` 直接读 raw 事件
- **位置**：`ai_hardware.c` 的 `accel_read()` 和注释

---

### Bug #10: 按键读取也受 GPIO 方向切换影响
- **症状**：`gpio_read(28)` 调用后，GPIO28 被设为输入，但之前如果被其他代码设为输出，方向就丢了
- **根因**：同 Bug #7 — `gpio_ensure()` 在 GPIO 已导出时跳过方向设置。修复后强制重设
- **位置**：`ai_hardware.c` 的 `gpio_ensure()`

---

### Bug #11: ARM 交叉编译兼容性
- **症状**：`bool`/`true`/`false` 未声明，`LV_LOG_USER` 变参宏警告
- **修复**：
  ```c
  #ifndef bool
    #define bool int
    #define true  1
    #define false 0
  #endif
  ```
  以及 `LV_LOG_USER("text")` → `LV_LOG_USER("%s", "text")`（单参数变参宏在不同编译器上的兼容性）
- **位置**：`ai_hardware.c` 头部

---

### Bug #12: 中文引号编译错误
- **症状**：System Prompt 中的 `"你好"` 中文双引号被 C 预处理器误解
- **修复**：改用角括号 `「你好」`
- **位置**：`app_actions.c` 的 `OLLAMA_SYSTEM_MSG`

---

### Bug #13: Think 面板宽度错误
- **症状**：思考面板继承了回答气泡的窄宽度
- **根因**：面板 `lv_pct(100)` 在窄容器内被限制
- **修复**：`min_width = BUBBLE_MAX_W - 20`，使用固定尺寸而非百分比
- **位置**：`ai_chat_page.c`

---

### Bug #14: 对话历史中 `[工具结果]` 前缀污染
- **症状**：AI 在回复中引用了 `[工具结果]` 前缀（"根据之前的[工具结果]..."）
- **根因**：历史注入的硬件执行结果带 `[工具结果]` 前缀，AI 学到了这个格式并在回复中模仿
- **修复**：移除 `[工具结果]` 前缀；LED/蜂鸣器结果不注入历史（只注入传感器数据）
- **位置**：`app_actions.c` 的 `ai_recv_thread()`

---

## 5. 当前代码框架

### 5.1 文件结构

```
src/ui-smart-water/pages/ai-chat-page/
├── ai_chat_page.c       # LVGL UI 页面 — 聊天气泡 + 思考面板 + 快捷按钮
├── ai_chat_page.h       # UI 页面接口
├── ai_hardware.c        # 硬件控制模块 — GPIO/LED/蜂鸣器/按键/加速度计 (~700行)
├── ai_hardware.h        # 硬件模块头文件 — GPIO定义 + API声明
├── wiki.md              # 本文档
├── http_client.c        # POSIX socket HTTP 客户端
└── http_client.h        # HTTP 客户端接口

src/ui-smart-water/pages/
└── app_actions.c        # 平台回调 + Ollama 通信 + 意图预检测 + 消息处理管道 (~600行AI部分)
```

### 5.2 核心 API

```c
// ── ai_hardware.h ──

// 执行单个硬件动作，返回 malloc 的中文结果字符串
char* ai_execute_action(const char *action_name);

// 扫描文本中的 [ACTION] 行，执行硬件并移除该行
// 返回执行结果汇总（malloc），无动作时返回 NULL
char* ai_split_actions(char *text);

// 仅移除 [ACTION] 行，不执行（用于清理对话历史）
void ai_strip_actions(char *text);
```

### 5.3 支持的硬件命令

| 命令 | 操作 | 返回值格式 |
|------|------|-----------|
| `led1_on` ~ `led4_on` | 开指定 LED | `[LED-D7] 已打开` |
| `led1_off` ~ `led4_off` | 关指定 LED | `[LED-D7] 已关闭` |
| `led_all_on` / `led_all_off` | 全开/全关 | `[LED] 全部已打开 (D7~D10)` |
| `led_random_on` | **智能**随机开一盏灭的 LED | `[LED] 无亮着, 从 4 盏灭的中随机打开 D7` |
| `buzzer_on` / `buzzer_off` | 蜂鸣器开/关 | `[蜂鸣器] 已鸣响` |
| `buzzer_toggle` | 蜂鸣器反转（软件状态跟踪）| `[蜂鸣器] 已切换为静音` |
| `read_button` | 读取按键 K2 状态 | `[按键-K2] 未按下` |
| `read_accel` | 读取三轴加速度 | `[加速度] X=12(~0.05g) Y=3(~0.01g) Z=-261(~-1.02g) — 10-bit原始值 ±2g量程` |

### 5.4 `led_random_on` 智能逻辑

```c
// 1. 扫描 4 路 LED 的 brightness 文件
// 2. 收集灭的 LED 到候选列表
// 3. 如果全部亮着 → 全部熄灭，返回
// 4. 从灭的列表中随机选一盏点亮
// 5. 返回详细状态（哪些亮着，从几盏灭的里选了哪盏）
```

### 5.5 关键设计原则

1. **硬件结果格式**：`[类型] 状态` — 简洁、不污染 AI 上下文
2. **调试信息分离**：详细数据走 `LV_LOG_USER`（串口），紧凑结果走返回值（AI 上下文）
3. **软件状态跟踪**：蜂鸣器等需要 toggle 的外设，用 `static` 变量跟踪状态，避免 GPIO 方向切换
4. **LED 子系统操作**：通过 `/sys/class/leds/ledX/brightness`，不通过裸 GPIO
5. **单次 UI 回调**：用 `lv_async_call` 单次回调保证 think → answer 的显示顺序

---

## 6. 深度思考：这算真正的 AI 控制硬件吗？

### 6.1 当前模式的本质

```
用户 "开灯" → C 代码 strstr → C 直接开灯 → 告诉 AI "已开了，你评论两句"
```

严格来说，**AI 不是决策者，是旁白**。所有硬件决策（什么时候开灯、开哪盏、何时读取传感器）都由 C 代码的 `if(strstr(...))` 决定。AI 没有 "选择权" — 它不能根据上下文决定 "这次开 D9 更合适"。

### 6.2 为什么这是被迫的折衷

DeepSeek-R1:7B（7B 参数）的核心能力是**推理**（`<think>` 思维链），不是**结构化输出**。让它输出精确的 `[ACTION]led3_on` 格式，对它是认知负担。14 个 bug 中有 5 个（#4 #5 #6 #8 #14）直接或间接源于这个能力的边界。

### 6.3 与 MCP / Function Calling 的本质相似性

用户说得对：**本质上和具备 MCP 接口的大模型操控硬件是差不多的**。

| 组件 | OpenAI Function Calling | 我们的方案 B |
|------|------------------------|------------|
| 工具描述 | JSON Schema in `tools` param | System Prompt 文本描述 |
| 工具调用 | 模型输出 `tool_calls` JSON | 模型输出 `[ACTION]cmd` 文本 |
| 调用解析 | SDK 自动解析 | C 代码 `strstr` + `strcmp` |
| 执行层 | 用户定义的 function | `ai_execute_action()` |
| 结果回注 | `tool` role message | `ai_hist_add("user", result)` |

架构模式是**同构**的：
1. 告诉模型有什么工具可用
2. 模型决定调用哪个工具
3. 外部代码执行工具
4. 执行结果返回给模型
5. 模型基于结果继续推理

区别在于：OpenAI 是在模型训练阶段就内化了工具调用格式（JSON Schema → tool_calls），而我们在 7B 模型的推理阶段用 prompt engineering 来模拟这个过程。**前者是训练时内化的能力，后者是推理时引导的能力** — 这就是为什么前者可靠而后者不可靠。

### 6.4 如果要真正实现 AI 驱动

需要满足以下条件之一：
1. **模型原生支持 function calling**（如 gpt-4、qwen3、Llama 3.1+）
2. **更大的模型**（13B+ 通常有更好的指令跟随能力）
3. **Fine-tuning**：用几百条 `[用户消息] → [ACTION]命令` 的训练样本微调 DeepSeek-R1
4. **多轮交互**：第一轮 AI 输出命令 → C 执行 → 结果注入 → 第二轮 AI 解释（1 个用户请求 = 2 次 HTTP 调用）

---

## 7. 与 MCP / OpenAI Function Calling 的对比

### 7.1 架构对比

```
OpenAI Function Calling:
┌──────────┐   tools=[{name:"led_on",...}]   ┌──────────────┐
│ 用户消息  │ ──────────────────────────────→ │ GPT-4 / 3.5  │
│          │                                  │              │
│          │  ← tool_calls=[{led_on,{}}]      │  (模型决策)   │
│ 执行硬件  │ ─── led_on() → "[LED]已打开" ──→ │              │
│          │                                  │              │
│          │  ← final: "好的,已开灯"           │  (模型解读)   │
└──────────┘                                  └──────────────┘

我们的方案 B (v3 混合模式):
┌──────────┐   strstr("开灯") → 执行         ┌──────────────┐
│ 用户消息  │ ─── C代码预检测 → led_on() ──→  │ DeepSeek-R1  │
│          │                                  │   :7B        │
│          │  "开灯\n(系统已执行:[LED]已开)"   │              │
│          │ ──────────────────────────────→  │  (看到结果)   │
│          │                                  │              │
│          │  ← "好的,已打开D7"               │  (自然回复)   │
└──────────┘                                  └──────────────┘
```

### 7.2 关键差异

| 维度 | OpenAI FC | 我们的方案 |
|------|----------|----------|
| 工具调用由谁发起 | **模型决定**何时调用、调哪个 | **C 代码 strstr 决定** |
| 模型是否有选择权 | ✅ 有 | ❌ 没有（C 代码预先拦截）|
| 格式可靠性 | 非常高（训练时内化）| 低（推理时引导，7B 模型不稳定）|
| 是否需要模型特殊支持 | 需要 | 不需要（任何模型都可以）|
| 上下文污染风险 | 低（JSON 结构清晰）| 高（文本混排，容易混淆）|

---

## 8. 经验教训

### 8.1 技术层面

1. **LED 子系统 ≠ GPIO**：内核 LED 驱动拦截了对应 GPIO，用户空间只能通过 `/sys/class/leds/` 操作。`brightness` 的 1/0 定义与硬件极性无关。

2. **GPIO 方向切换是破坏性操作**：`echo in > direction` 会关闭输出驱动，之后读回的值不可信。需要 toggle 的外设应该用软件状态跟踪。

3. **ioctl 的 min/max 不可盲信**：`EVIOCGABS` 返回的校准值可能与芯片实际量程不一致。对传感器，应直接看原始数据，用物理验证（重力 = 1g）来校准。

4. **AI 上下文是稀缺资源**：每字节传感器 debug 数据进入对话历史，都会在后续所有回合消耗 token 预算并增加模型混淆风险。传感器输出必须紧凑化。

5. **System Prompt 的每一个词都可能被模型模仿**：如果 prompt 里描述了 `[ACTION]` 格式，模型就会倾向于输出 `[ACTION]`。如果 prompt 里有 `[工具结果]`，模型就会引用它。

### 8.2 架构层面

6. **7B 模型 ≠ 7B 模型 + function calling**：推理能力和结构化输出是两种不同的能力。DeepSeek-R1:7B 前者的能力强，后者基本不具备。

7. **先验证硬件，再写代码**：LED 极性问题浪费了大量时间。如果一开始就用 `echo 1 > brightness` 验证，就不会写反。

8. **意图预检测是务实的选择**：在模型不支持 function calling 的前提下，C 代码预检测 + AI 解读的混合模式是工程上最可靠的方案。虽然 AI 不是真正的 "决策者"，但它仍然在 "理解并解读硬件状态" 这个环节提供了真正的价值。

9. **架构的简洁性直接影响可靠性**：v1 (XML) → v2 ([ACTION]) → v3 (意图预检测)，每次架构简化都带来了可靠性的显著提升。

### 8.3 调试层面

10. **串口日志是嵌入式 AI 调试的唯一窗口**：所有 debug 信息走 `LV_LOG_USER`，包括 AI 回复的 hex dump、命令提取过程、硬件执行结果。

11. **先在 PC 上用 stub 验证 UI 流程**，再在 ARM 板上验证硬件：PC stub 返回模拟值，可以独立验证 UI 逻辑。硬件 bug 和 UI bug 分开排查。

---

## 9. 未来升级路线

1. **Phase 1 (当前)**：C 代码意图预检测 + AI 自然解读
2. **Phase 2**：扩展意图检测表，覆盖更多用户表述（如 "把第三个灯灭掉" → led3_off）
3. **Phase 3**：当 DeepSeek 官方更新支持 tool_calls，或换用 qwen3:8b → 切换到原生 function calling
4. **Phase 4**：多轮交互 — AI 第一轮出命令 → C 执行 → 第二轮 AI 解读（真正 AI 驱动）
5. **Phase 5**：MCP Server 统一管理硬件工具，支持多模型接入
6. **Phase 6**：AI 自动巡检 — 定时读取传感器 → 分析趋势 → 主动告警

---

## 文件清单

| 文件 | 说明 |
|------|------|
| `ai_hardware.h` | 硬件模块头文件 — GPIO 定义 + API 声明 |
| `ai_hardware.c` | 硬件模块实现 — `ai_execute_action()` + `ai_split_actions()` + `ai_strip_actions()` |
| `app_actions.c` | 调用方 — Ollama 通信 + 意图预检测 + 消息处理管道 |
| `ai_chat_page.c` | LVGL UI — 聊天气泡 + 思考面板 + 快捷按钮 |
| `wiki.md` | 本文档 — 完整开发日志 |

---

> 最后更新：2026-06-30
> 许可证：MIT
