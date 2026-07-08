# 边缘端数据预处理引擎

> **一句话概括**：把原来「MQTT 收数据 → 直接显示」改成「MQTT 收数据 → 本地清洗 → 存入硬盘 → 分析预测 → 显示」。断网不丢数据，重启自动恢复，板子自己就能做趋势分析和异常过滤，不依赖云端。

---

## 目录

1. [这个模块做什么](#这个模块做什么)
2. [小白名词解释](#小白名词解释)
3. [架构总览](#架构总览)
4. [数据流全链路](#数据流全链路)
5. [各组件详解](#各组件详解)
   - [sensor_range.h —— 量程配置表](#1-sensor_rangeh--量程配置表)
   - [edge_engine.c —— 引擎核心](#2-edge_enginec--引擎核心)
   - [edge_store.c —— 存储层](#3-edge_storec--存储层)
   - [edge_analysis.c —— 数学分析](#4-edge_analysisc--数学分析)
6. [关键问答 (答辩备忘)](#关键问答-答辩备忘)
7. [对外的公共 API](#对外的公共-api)
8. [如何移植到别的工程](#如何移植到别的工程)
9. [文件清单](#文件清单)

---

## 这个模块做什么

这是"智能水产养殖系统"的边缘计算层，跑在 GEC6818 ARM 开发板上，负责在**本地**完成四件事：

| 环节 | 做了什么 | 对应文件 |
|------|---------|---------|
| **数据清洗** | 去重、物理量程过滤、速率突变检测，把传感器误采样的无效数据标记出来 | `edge_engine.c` → `classify()` |
| **本地持久化** | 原始数据全部写入板载 eMMC 的 CSV 文件，掉电/断网不丢失 | `edge_store.c` → `store_append_raw()` |
| **智能分析** | 每隔 60 秒跑一次最小二乘线性回归 + Pearson 相关性矩阵，预测 1 小时后的趋势 | `edge_analysis.c` |
| **异常阈值告警** | 每路传感器独立可配告警阈值（warn_lo/hi），超限计入日报剔除数；传感器页可实时调节并持久化 | `sensor_range.h` → `sensor_warn_t`, `edge_store.c` → `store_compute_daily()` |
| **对外提供查询** | 给 LVGL 界面层提供快照（实时值）、历史曲线、分析报表、阈值读写四个接口 | `edge_engine.h` |

---

## 小白名词解释

这一节写给不熟悉底层概念的读者。

### 1. eMMC

**embedded MultiMediaCard**，就是**焊在板子上的"硬盘"**。

GEC6818 开发板标配 **8GB 或 16GB 的 eMMC 芯片**，Linux 系统本身（内核 + 根文件系统）就装在上面。你写的程序、存的 `/root/aqua_data/` 目录，全都落在这块芯片上。它的底层技术跟 SD 卡一样（都是 NAND Flash），区别只是**物理形态**——SD 卡可以拔插，eMMC 是直接焊死的。

**掉电后数据还在吗？** 在。Flash 存储是非易失的，断电不丢数据。你之前视频播放器读的 `/root/videos/1.mp4` 也是一样存在同一块 eMMC 上。

### 2. fsync

`fsync()` 是一个 Linux 系统调用，作用是把**操作系统的"写缓存"强制刷到物理硬件上**。

写文件时，`fprintf` 写完数据并不会立刻到达 eMMC 芯片。路径是：

```
fprintf → C语言库缓冲区 → Linux内核缓存 → eMMC硬件芯片
              ↑              ↑              ↑
          fflush把数据      fsync把数据     真正掉电安全的
          推到内核          推到硬件        位置
```

如果不调 `fsync`，断电时核心里还没刷到硬件的数据就会丢。我们的引擎每写 **10 行 CSV**（≈30 秒）调一次 `fsync`，这意味着**最多丢最近半分钟的数据**。

### 3. 快照 (Snapshot)

快照就是**每路传感器最后一次有效值的副本**，存在内存里：

```
g_latest[温度] = { value: 25.3°C, ts: 1783332021, valid: true }
g_latest[湿度] = { value: 62.1%,  ts: 1783332021, valid: true }
...
```

sensor_page 的 6 张卡片每秒刷一次，直接从快照读——不需要每次去扫文件。快照只在收到 flag=0（有效）数据时更新，异常帧不更新快照。重启时通过**扫当日 CSV 尾部**重建。

### 4. Flag (数据标志位)

CSV 每行最后一列的那一个数字，标记本条数据的质量判定：

| Flag | 枚举名 | 含义 | 会不会进分析 |
|------|--------|------|:-----------:|
| **0** | `EDGE_FLAG_VALID` | 正常，通过全部检查 | ✅ 是 |
| **1** | `EDGE_FLAG_RANGE` | 超出物理量程（如温度 999°C） | ❌ 否 |
| **2** | `EDGE_FLAG_SPIKE` | 速率突变（如氨氮 3 秒跳 0.8） | ❌ 否 |
| **3** | `EDGE_FLAG_DUPE` | 重复帧（MQTT 重复投递 / 传感器卡死） | ❌ 否 |

**原始数据不管 flag 是多少全部写入 CSV**（满足"所有原始数据持久化"）。**但回归/相关/日报只看 flag=0 的**（满足"剔除无效数据"）。

### 5. 最小二乘线性回归

一种数学方法，找一条直线 `y = a + b·t`，让这条线到所有数据点的总距离（平方和）最小。`b` 是斜率：b > 0 表示趋势上升，b < 0 表示下降。然后用这条直线外推，估算 1 小时后的大概值。

不需要云端 GPU，在 GEC6818 的 ARM CPU 上算 600 个点只需几十微秒。

### 6. Pearson 相关系数

用 `r` 表示，取值范围 -1 到 +1。|r| 越接近 1，两个变量越"相关"：
- r ≈ +1：一个涨另一个也涨（如温度↔光照）
- r ≈ -1：一个涨另一个跌（如温度↔溶解氧，水温越高氧气溶解度越低）
- r ≈ 0：没什么关系

引擎只保留 |r| ≥ 0.5 的结果。

### 7. 线程安全

在多线程程序里，两个线程同时写同一块内存会导致**数据竞争**（读到的值说不清是修改前还是修改后）。我们用了 `pthread_mutex_t`（互斥锁）保护共享数据：任何线程要读写共享数据前，必须先拿到锁。持锁时间极短（微秒级），不会影响 UI 刷新。

---

## 架构总览

```
┌─────────────────┐      ┌─────────────────────┐      ┌────────────────┐
│   MQTT broker   │─────▶│   app_mqtt.c         │─────▶│ edge_engine.c  │
│ (emqx.io:1883)  │ 3s   │ Paho 客户端回调线程   │ push │ 队列 → worker │
└─────────────────┘      └─────────────────────┘      └───────┬────────┘
                         仅在 APP_USE_MQTT 编译               │
                                                             ▼
    ┌──────────────────────────────────────────────────────────────┐
    │                    process_sample()                           │
    │                                                               │
    │  ① classify()  去重 + 量程 + 突变 → flag ∈ {0,1,2,3}        │
    │  ② store_append_raw()  → /root/aqua_data/raw/日期.csv        │
    │  ③ 若 flag==0: 更新 g_latest[6] 快照 + g_ring[6][480] 历史环 │
    │  ④ 每 60s: recompute_analysis() → 回归 + 相关 + 日报         │
    └──────────┬───────────────────────────────────────────────────┘
               │
      ┌────────┴──────────┐
      │  edge_engine API  │  ← 短锁拷出，不阻塞
      └────────┬──────────┘
               │
    ┌──────────┼──────────────────────────────┐
    │          │         LVGL 主线程            │
    │          │                                │
    │  sensor_page.c    trend_page.c            │
    │  get_latest()     get_history()           │
    │  每 1s 刷 6 卡片   get_analysis()         │
    │                  每 5s 刷曲线+预测+日报     │
    └───────────────────────────────────────────┘
```

**目录结构**：

```
/root/aqua_data/                  ← 板子 eMMC 上的根目录
  ├── raw/
  │   ├── 2026-07-06.csv           ← 每天一个 CSV，append-only
  │   └── 2026-07-07.csv
  └── analysis/
      ├── latest.txt               ← 最新的分析缓存（回归 + 相关 + 日报）
      └── warn_thresholds.txt      ← 用户配置的异常告警阈值 (每路 lo/hi/enabled)
```

---

## 数据流全链路

从一条 MQTT 消息到屏幕显示，总共经过以下步骤：

```
步骤 1: MQTT 接收
  mqtt_pub_demo.c 每 3 秒发一条 JSON：
  {"seq":42, "temperature":25.3, "humidity":62.1, "light":45,
   "do":5.2, "ph":7.1, "nh3n":0.12}
  → app_mqtt.c 的 mqtt_msg_arrived() 回调收到
  → cJSON 解析 → 构建 raw_sample_t{ts=time(NULL), seq=42, values[6]}
  → edge_engine_push(&sample)  非阻塞，立即返回

步骤 2: 入队 (仅 Linux/ARM)
  push() 加锁把 sample 放入环形队列 (容量 256)，signal condvar 唤醒 worker
  → 若队列满，丢弃最旧一条（永远不会阻塞 MQTT 回调线程）
  → Windows/PC 上无队列，直接同步处理

步骤 3: 清洗分类
  worker 线程取到 sample → classify():
    ├─ seq 与上条相同？→ flag=DUPE，跳过
    ├─ 6 路值全部与上条差 <0.001？→ flag=DUPE，跳过
    ├─ 某路超出 [phys_min, phys_max]？→ flag=RANGE，跳过
    └─ 某路跳变 > max_delta？→ flag=SPIKE，跳过
  → 全部通过 → flag=VALID

步骤 4: 写入 eMMC
  store_append_raw(&sample, flag):
    FILE *f = fopen("/root/aqua_data/raw/2026-07-06.csv", "a");
    fprintf(f, "ts,seq,6路值,flag\n");
    → 每 10 条 fsync(fileno(f)) 一次，强制刷到 eMMC 硬件
    → fclose(f)

步骤 5: 更新内存状态 (仅 flag==VALID)
  g_latest[温度] = { value: 25.3, ts: 1783332021, valid: true }   ← 快照
  g_latest[湿度] = { value: 62.1, ts: 1783332021, valid: true }
  ... (共 6 路)
  g_ring[温度][head] = 25.3   ← 历史环，滚动覆盖，容量 480 点 (≈24分钟)

步骤 6: 周期分析 (每 60s)
  recompute_analysis(now):
    ├─ 从 CSV 读最近 600 条有效样本
    ├─ 每路跑最小二乘 → slope + pred_1h
    ├─ 两两跑 Pearson 相关 → 按 |r| 降序
    └─ 扫当日 CSV 全量 → 每路 min/max/mean/剔除数

步骤 7: LVGL 显示
  sensor_page (1s 定时器):  g_latest[idx] → 卡片显示当前值
  trend_page  (5s 定时器):  g_ring[idx][0..n-1] → lv_chart 画曲线
                            g_analysis → 预测文本 + 相关性列表 + 日报表
```

---

## 各组件详解


### 1. sensor_range.h — 量程配置表

**文件**: `src/ui-smart-water/edge/sensor_range.h`

**作用**: 定义 6 路传感器的枚举索引和物理约束表，是 engine 和 UI 共用的唯一头文件。

```c
typedef enum {
    SENSOR_IDX_TEMP = 0,   // 温度     °C
    SENSOR_IDX_HUMI,       // 湿度     %
    SENSOR_IDX_LIGHT,      // 光照     %
    SENSOR_IDX_DO,         // 溶解氧   mg/L
    SENSOR_IDX_PH,         // pH值     —
    SENSOR_IDX_NH3N,       // 氨氮     mg/L
    SENSOR_IDX_COUNT       // 共 6 路
} sensor_idx_t;
```

**量程表**：

| 传感器 | 物理最小 | 物理最大 | 最大突变/3s | 显示小数位 |
|--------|:-------:|:-------:|:----------:|:---------:|
| 温度 | 0 °C | 40 °C | 5.0 | 1 |
| 湿度 | 0 % | 100 % | 20.0 | 1 |
| 光照 | 0 % | 100 % | 60.0 | 0 |
| 溶解氧 | 0 mg/L | 20 mg/L | 3.0 | 1 |
| pH值 | 0 | 14 | 1.0 | 2 |
| 氨氮 | 0 mg/L | 5 mg/L | 0.5 | 2 |

这些阈值是基于水产养殖场景的经验值。传感器类型不同（水产用 VS 空气用），阈值也不同——所以这个表是整个模块唯一需要"按场景定制"的地方。

#### 异常告警阈值 (v2 新增)

**`sensor_warn_t`** — 每路传感器独立的可配置告警阈值：

```c
typedef struct {
    float warn_lo;     /* 异常下限 (低于此值 → 告警+计入剔除) */
    float warn_hi;     /* 异常上限 (高于此值 → 告警+计入剔除) */
    bool  enabled;     /* 是否启用告警 */
} sensor_warn_t;

extern sensor_warn_t g_sensor_warn[SENSOR_IDX_COUNT];
```

**默认值** (水产养殖参考):

| 传感器 | warn_lo | warn_hi |
|--------|:------:|:------:|
| 温度 | 15.0 °C | 32.0 °C |
| 湿度 | 30.0 % | 90.0 % |
| 光照 | 10.0 % | 100.0 % |
| 溶解氧 | 3.0 mg/L | 20.0 mg/L |
| pH值 | 6.0 | 9.0 |
| 氨氮 | 0.0 mg/L | 1.0 mg/L |

**与物理量程的区别**:
- `phys_min/phys_max` = 传感器硬件极限 (如温度 0~40°C)，超出=硬件故障，`classify()` 标记 RANGE
- `warn_lo/warn_hi` = 养殖水质安全范围 (如温度 15~32°C)，超出=水质异常，计入日报剔除数 + 传感器页变红

**读写 API**:
```c
bool edge_engine_get_warn(sensor_idx_t idx, float * lo, float * hi);
void edge_engine_set_warn(sensor_idx_t idx, float lo, float hi);  /* 立即持久化 */
```

传感器页点卡片 logo → 弹窗拖 slider 调节 → 实时写回引擎 + 落盘 `warn_thresholds.txt`。重启自动加载。


### 2. edge_engine.c — 引擎核心

**文件**: `src/ui-smart-water/edge/edge_engine.c` (622 行)

**职责**：接收数据 → 清洗 → 入环 → 周期分析。对外暴露 get 接口。

**线程模型**：

| 平台 | 模式 | 说明 |
|------|------|------|
| Linux/ARM (GEC6818) | worker 线程 | 队列 256 槽 + condvar 阻塞等待 |
| Windows/PC | 单线程内联 | `push()` 直接调 `process_sample()`，便于演示 |

#### classify() —— 异常检测算法

```
输入: raw_sample_t *s (含 6 路值 + seq + 时间戳)
输出: flag ∈ {0,1,2,3}

算法流程:
  1. 去重 - seq
     if s->seq == g_last_valid.seq → DUPE
     (MQTT broker 偶尔会重复投递同一消息，用 seq 号检测)

  2. 去重 - 传感器卡死
     for i in 0..5:
       if |s->values[i] - g_last_valid.values[i]| > 0.001 → 不同
     → 若全部 ≤0.001 → DUPE
     (传感器物理卡住，6 路值连续不变)

  3. 物理量程
     for i in 0..5:
       if s->values[i] < phys_min[i] or > phys_max[i] → RANGE
     (传感器短路/开路/ADC 溢出等硬件故障)

  4. 速率突变
     for i in 0..5:
       if |s->values[i] - g_last_valid.values[i]| > max_delta[i] → SPIKE
     (传感器瞬间跳变超过物理可能的最大值，如氨氮 3 秒内从 0.12 跳到 0.95)

  5. 全部通过 → VALID
```

**注意**：异常帧**不更新** `g_last_valid` 和快照，防止异常值"污染"后续判断。只有 VALID 帧才能成为比较基准。


### 3. edge_store.c — 存储层

**文件**: `src/ui-smart-water/edge/edge_store.c` (159 行)

**职责**：所有磁盘 I/O。不依赖 cJSON（纯文本分析缓存），不依赖 LVGL，不依赖 Paho。

**CSV 格式**：

```csv
# /root/aqua_data/raw/2026-07-06.csv
# ts,seq,temp,humi,light,do,ph,nh3n,flag
1783331983,0,25.300,62.100,45.000,5.200,7.100,0.120,0
1783331986,1,33.800,54.000,55.000,8.700,7.300,0.870,0
1783332021,2,29.100,73.300,6.000,8.000,6.800,0.040,2     ← NH3N 跳变 0.83>0.5，flag=SPIKE
```

- 每天一个文件，文件名 = 日期 (`YYYY-MM-DD.csv`)
- 追加模式 (`fopen(..., "a")`)，绝不覆盖已有数据
- 每 10 条 `fsync` 一次 → 最多丢 30 秒数据
- 分析缓存用纯文本 (`analysis/latest.txt`)，sscanf 解析，不引入 JSON 库依赖
- 告警阈值用纯文本 (`analysis/warn_thresholds.txt`), 格式 `warn <idx> <lo> <hi> <enabled>`, 启动加载 + set_warn 时即时落盘

**日报剔除计数 (v2)**: `store_compute_daily()` 扫描 CSV 时对 flag=VALID 的样本额外检查每路值是否超出 `g_sensor_warn[i].warn_lo/hi`，超限也计入 `daily[i].reject`。与传感器页红绿判定用同一套阈值，保证语义一致。

**文件名怎么生成**：

```c
time_t t = time(NULL);                         // 拿当前时间
struct tm tmv;
localtime_r(&t, &tmv);                         // 转成年月日
sprintf(buf, "/root/aqua_data/raw/%04d-%02d-%02d.csv",
        tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday);
```

过 0 点自动切到新文件，旧文件留在磁盘上。

**为什么不用 SQLite？**

SQLite 是一个轻量嵌入式数据库，功能比 CSV 强（SQL 查询、事务、并发安全）。但：
- 需要交叉编译 `sqlite3.c`（约 9MB 的 amalgamation 源码）
- 现有工程已经有 Paho (MQTT)、cJSON、FreeType 等交叉编译依赖，再加一个会增加维护成本
- 每天约 28800 条数据，扫全文件 ≈ 1MB 读取，250ms 内完成，CSV 性能完全够用

遵循项目现有的哲学：能用源码 vendoring / 标准 C 库解决的问题，不加新依赖。


### 4. edge_analysis.c — 数学分析

**文件**: `src/ui-smart-water/edge/edge_analysis.c` (67 行)

**职责**：纯数学计算，零依赖（只用了 `<math.h>` 的 `sqrt` 和 `fabs`）。**可以拿出来单独编译和单元测试**。

#### 最小二乘线性回归

公式：

```
给定 n 个点 y[0], y[1], ..., y[n-1], t = 0, 1, ..., n-1

均值:  t̄ = (n-1)/2       (因为 t 是等差数列 0,1,...,n-1)
       ȳ = Σy[i] / n

斜率:  b = Σ(t-t̄)(y-ȳ) / Σ(t-t̄)²
截距:  a = ȳ - b·t̄

拟合直线: y = a + b·t
原始外推:  pred_1h_raw = a + b × (n + 1200)     (1200 = 3600秒/3秒间隔)

预测值施加两重约束后再存入 regr_t.pred_1h:

 ① 偏离约束 — 预测值偏离当前值不超过量程 20%。
    解决问题：冷启动时样本少(n≈20)，噪声斜率被 1200 步外推放大，
    湿度可能从 65% 预测到 0%、温度从 25°C 预测到 5°C。
    约束后湿度最多降到 45%(65-20)、温度最多降到 17°C(25-8)。
    随着数据积累到 600 点(30min)，斜率稳定，约束基本不再触发。

 ② 量程钳制 — 预测值最终钳制到 [phys_min, phys_max]，
    防止氨氮预测为负值、湿度预测 >100% 等物理不可能值。

约束 C 代码分两处: `edge_engine.c` recompute_analysis() (引擎层) + `trend_page.c` refresh_analysis() (显示层); 回归公式本身的纯数学实现在 `edge_analysis.c` 第 34-59 行。
```

**数值稳定性**：
- 用 `double`（64 位浮点）做累加，避免 32 位 `float` 的精度损失
- 中间量 t̄ 用公式算出（不用循环累加），减少浮点舍入误差
- n < 2 时直接返回（至少两个点才能画线）

#### Pearson 相关系数

公式：

```
r = Σ(a_i - ā)(b_i - b̄) / √[Σ(a_i - ā)² · Σ(b_i - b̄)²]

分子 = 协方差（两个变量一起变的程度）
分母 = 各自标准差的乘积（归一化，让结果在 -1 ~ +1 之间）
```

**水产养殖中的典型相关性**：
- **温度 ↔ 溶解氧 负相关** (r ≈ -0.7)：水温越高，氧气溶解度越低
- **光照 ↔ 温度 正相关** (r ≈ +0.5)：阳光照射提高水温
- **pH ↔ 氨氮**：可能因为氨的形态转化产生弱相关

---

## 关键问答 (答辩备忘)

以下内容整理自开发过程中的实际答疑。

### Q1: eMMC 持久化是怎么实现的？

打开一个本地 CSV 文件，每次收到数据就追加一行。每 10 行调一次 `fsync()` 把 Linux 核心里缓存的数据强制刷到 eMMC 物理芯片上，确保掉电时最多只丢最近 30 秒的数据。

### Q2: MQTT 断联会怎样？

断线时 g_connected = false，传感器页面**不变成 "--"**，而是保留引擎快照里的最后有效值。sensor_page 的 1 秒定时器每 10 秒调一次 `app_mqtt_ensure_connected()`，检测到断线就自动重连 broker，重新订阅主题，恢复收数据。

### Q3: 数据是先处理再写入，还是先写入再处理？

先处理（`classify()` 做去重+量程+突变检测，得到 flag），然后**原始值 + flag 全部写入 CSV**。flag=0 的有效帧才更新快照和历史环，flag≠0 的异常帧只落盘不进分析。

### Q4: Flag 是什么意思？

CSV 最后一列的数字，标记每条数据的质量：0=正常，1=超量程，2=突变，3=重复。所有数据都存，但分析只看 flag=0。

### Q5: 快照是什么？

每路传感器"最近一次有效值"的内存副本。sensor_page 每秒刷新时直接从快照读，不需要每次扫文件。重启时从 CSV 尾部回读重建。

### Q6: edge/ 文件夹能不能移植到别的工程？

**能。** 整个文件夹零外部依赖：不依赖 LVGL、不依赖 Paho (MQTT)、不依赖 cJSON。唯一需要改的是 `sensor_range.h` 里的枚举 + 量程表，换成你自己工程传感器的定义。移植只需三步：拷贝文件夹 → CMake 加 glob → 调 API。

### Q7: 预测值会不会出现负数或超过 100%？

不会。pred_1h 在引擎计算后经过两重约束才对外暴露：

1. **偏离约束** — 预测值偏离当前值不超过量程的 20%。这防止了冷启动时（样本少、斜率噪声大）1h 外推被 1200 步放大到荒谬值（如湿度从 65% 预测到 0%、温度从 25°C 预测到 5°C）。数据积累到 30min(≈600 点)后斜率自然稳定，约束基本不再触发。
2. **量程钳制** — 最终值钳制到 [phys_min, phys_max]。氨氮预测不会为负、湿度预测不会 >100%。

两重约束在引擎层 (`edge_engine.c` recompute_analysis) 和 UI 显示层 (`trend_page.c` refresh_analysis) 各做一次，既保证存储的数据正确，也防止旧缓存里的越界值被显示。

### Q8: 为什么用 CSV 而不用 SQLite？

避免增加交叉编译依赖。标准 C 库 `fopen/fprintf/fclose` 零依赖，每天 ~28800 行数据扫描在毫秒级完成，性能完全够用。SQLite 的 amalgamation 约 9MB 源码，会增加板子构建脚本的复杂度。

---

## 对外的公共 API

以下接口全部在 `edge_engine.h` 中声明，LVGL 界面层通过 `app_actions.c` 转发调用。

### 生命周期

```c
#include "edge_engine.h"

/* 启动引擎。建目录、启动 worker 线程、从 CSV 回读快照+历史、加载分析缓存。
 * 应在 MQTT 初始化和 UI 页面创建之前调用。 */
bool edge_engine_init(void);

/* 停止引擎。通知 worker 退出、等待队列排空、落最后一次分析缓存。 */
void edge_engine_deinit(void);
```

### 写入数据 (生产者)

```c
/* MQTT 回调线程 / PC 模拟定时器 调用。
 * 非阻塞: Linux 入队列即返回, PC 直接同步处理。
 * 调用前把 MQTT JSON 解析成 raw_sample_t:
 *
 *   raw_sample_t s;
 *   s.ts  = (uint32_t)time(NULL);        // 到达时间戳
 *   s.seq = json_seq;                     // 发布端序号, 用于去重; 无则填 -1
 *   for(int i=0; i<6; i++)
 *       s.values[i] = json_parsed_value;  // 6 路传感器值
 *   edge_engine_push(&s);
 */
void edge_engine_push(const raw_sample_t * s);

/* PC 无 MQTT 时用于演示。
 * 采用平滑随机游走 (每帧 ±小步长, 钳制到贴近真实的工作区间),
 * 约 1/60 概率在 temp/DO/pH/NH3N 上注入一次真实突变以演示 flag=2 检测。
 * 旧实现为每帧独立均匀随机 → 相邻帧差必超 max_delta → 75% 误判突变,
 * 日报"剔除"动辄上百。改成游走后剔除数回到 0~3 的合理范围。
 * 需每 3 秒由 LVGL 定时器调一次。 */
void edge_engine_sim_feed(void);
```

### 读取数据 (消费者)

```c
/* ———① 读快照：传感器页面每 1s 刷新 6 张卡片 ———
 * 返回最后一条有效值。无数据时返回 false (卡片显示 "--")。
 *
 * 示例:
 *   float v;
 *   if(edge_engine_get_latest(SENSOR_IDX_TEMP, &v, NULL)) {
 *       printf("当前温度: %.1f°C\n", v);
 *   }
 */
bool edge_engine_get_latest(sensor_idx_t idx, float * value, uint32_t * ts);

/* ———② 读历史：趋势页面 lv_chart 画曲线 ———
 * 拷出最近 max 条有效值到 buf[], 返回实际条数 (0..max)。
 * 数据已按时间正序排列 (最旧的在前)。
 *
 * 示例:
 *   float buf[180];
 *   int n = edge_engine_get_history(SENSOR_IDX_PH, buf, 180);
 *   // 把 buf[0..n-1] 转为 int ×10 喂给 lv_chart
 */
int edge_engine_get_history(sensor_idx_t idx, float * buf, int max);

/* ———③ 读分析报表：趋势页面预测 + 相关性 + 日报 ———
 * 拷出分析缓存的副本。无数据时返回 false。
 *
 * 示例:
 *   edge_analysis_t a;
 *   if(edge_engine_get_analysis(&a)) {
 *       // 预测: a.regr[SENSOR_IDX_TEMP].pred_1h  (1小时后预测温度)
 *       // 相关性: a.corr[i].a, a.corr[i].b, a.corr[i].r  (传感器对 + 系数)
 *       // 日报: a.daily[SENSOR_IDX_TEMP].min/max/mean/count/reject
 *   }
 */
bool edge_engine_get_analysis(edge_analysis_t * out);

/* ———④ 读写异常告警阈值 (v2 新增, 传感器页 + 趋势页共用) ———
 * get: 返回 enabled 状态 (false=从未配置), lo/hi 写入传出指针
 * set: 立即持久化到 analysis/warn_thresholds.txt, 下次重启自动加载
 *
 * 示例:
 *   float lo, hi;
 *   edge_engine_get_warn(SENSOR_IDX_TEMP, &lo, &hi);
 *   edge_engine_set_warn(SENSOR_IDX_TEMP, 22.0f, 30.0f);
 */
bool edge_engine_get_warn(sensor_idx_t idx, float * lo, float * hi);
void edge_engine_set_warn(sensor_idx_t idx, float lo, float hi);
```

### 分析结果结构体

```c
typedef struct {
    uint32_t  ts;             // 生成时间
    regr_t    regr[6];        // 每路回归结果
    corr_pair_t corr[15];     // 相关性配对 (按 |r| 降序)
    int       corr_n;         // 实际配对数
    daily_t   daily[6];       // 每路当日汇总
    bool      valid;
} edge_analysis_t;

// 回归:
//   slope, intercept → 拟合直线 y = a + b·t
//   cur              → 当前值
//   pred_1h          → 1 小时后预测值
//   valid            → 是否有效 (样本≥2 才有效)
typedef struct {
    float slope, intercept;
    float cur, pred_1h;
    bool  valid;
} regr_t;

// 相关性对:
//   a, b → 传感器索引
//   r   → Pearson 系数 [-1, 1]
typedef struct {
    int8_t a, b;
    float  r;
} corr_pair_t;

// 当日汇总:
//   min, max, mean → 最小 / 最大 / 均值
//   count          → 有效样本数 (flag=0)
//   reject         → 被剔除样本数 (flag≠0)
typedef struct {
    float min, max, mean;
    int   count, reject;
    bool  valid;
} daily_t;
```

---

## 如何移植到别的工程

**edge/ 文件夹不依赖 LVGL、Paho、cJSON**，可以整体搬迁到任何 C 工程。

### 三步移植

#### 步骤 1: 拷贝文件夹

```bash
cp -r src/ui-smart-water/edge/  你的项目/src/edge/
```

#### 步骤 2: 构建系统加入 edge/*.c

**CMake** (推荐):
```cmake
file(GLOB EDGE_SRC src/edge/*.c)
list(APPEND MY_TARGET_SOURCES ${EDGE_SRC})
```

**Makefile**:
```makefile
EDGE_SRC = $(wildcard src/edge/*.c)
```

**直接编译** (无构建系统):
```bash
gcc -c src/edge/*.c -I src/edge
```

#### 步骤 3: 改 sensor_range.h

把枚举 `sensor_idx_t` 和量程表 `g_sensor_phys[]` 换成你工程里的传感器定义：

```c
// 假设你的工程有 3 个传感器：气压、风速、降雨量
typedef enum {
    SENSOR_IDX_PRESSURE = 0,
    SENSOR_IDX_WIND,
    SENSOR_IDX_RAIN,
    SENSOR_IDX_COUNT
} sensor_idx_t;

const sensor_phys_t g_sensor_phys[SENSOR_IDX_COUNT] = {
    [SENSOR_IDX_PRESSURE] = { "气压",   "hPa",   900, 1100, 10.0f, 1 },
    [SENSOR_IDX_WIND]     = { "风速",   "m/s",   0,   60,   15.0f, 1 },
    [SENSOR_IDX_RAIN]     = { "降雨量", "mm/h",  0,   200,  50.0f, 1 },
};
```

#### 步骤 4: 调用 API

```c
int main() {
    edge_engine_init();                       // 启动引擎

    // ... 在你的数据接收回调里:
    raw_sample_t s;
    s.ts = time(NULL); s.seq = 0;
    s.values[SENSOR_IDX_PRESSURE] = 1013.2;
    // ... 填充其他传感器
    edge_engine_push(&s);                     // 喂数据

    // ... 在你的显示线程里:
    float v;
    edge_engine_get_latest(SENSOR_IDX_PRESSURE, &v, NULL);
    printf("当前气压: %.1f hPa\n", v);        // 读快照

    edge_analysis_t a;
    edge_engine_get_analysis(&a);             // 读分析报表

    edge_engine_deinit();                     // 退出时清理
}
```

### 依赖清单

| 依赖 | 说明 |
|------|------|
| C99 编译器 | GCC / Clang / MSVC 均可 |
| `<stdio.h>` `<stdlib.h>` `<string.h>` | 标准 C 库，任何平台都有 |
| `<math.h>` | `sqrt()` 和 `fabs()`，任何平台都有 |
| `<time.h>` | `time()` 和 `localtime()`，用于时间戳和日期路径 |
| `<pthread.h>` | 仅 Linux/ARM 需要（多线程），Windows 下自动降级为单线程 |
| `<sys/stat.h>` `<direct.h>` | 创建目录用，Windows 和 Linux 各自有对应的 |

**没有** LVGL、Paho、cJSON、SDL2 等任何项目特定依赖。

---

## 文件清单

```
src/ui-smart-water/edge/
├── sensor_range.h       (~55 行) 共享枚举 + 物理量程表 + 异常告警阈值结构体
├── edge_engine.h        (~105 行) 公共 API + 数据结构定义 (含 get/set_warn)
├── edge_engine.c        (~430 行) 引擎核心: worker + 队列 + 清洗 + 快照/环 + 周期分析 + 阈值默认值
├── edge_store.h         (~48 行)  存储层头文件 (含 warn 持久化声明)
├── edge_store.c         (~340 行) 存储层: CSV + 分析缓存 + 告警阈值持久化 + 日报剔除含异常阈值检查
├── edge_analysis.h      (27 行)   数学分析头文件
├── edge_analysis.c      (66 行)   Pearson 相关 + 最小二乘回归 (纯数学, 可单测)
└── README.md            (本文档)
```

**配套 UI 页面** (仅 LVGL 工程使用):
```
src/ui-smart-water/pages/trend-page/
└── trend_page.c         (702 行) lv_chart 曲线 + 下拉选传感器 + 预测/相关/日报文本 + Faded area 渐变填充
```

**关联改动文件** (连线层):
```
app_actions.h / app_actions.c  → sensor_read 从引擎读快照
app_mqtt.c                     → 收到 JSON 后调 edge_engine_push()
home_page.h / home_page.c      → 第 6 个导航按钮 "趋势报表"
ui.c                           → edge_engine_init() + trend_page_create() + PC sim_feed
CMakeLists.txt                 → file(GLOB LV_EDGE_SRC src/ui-smart-water/edge/*.c)
```
