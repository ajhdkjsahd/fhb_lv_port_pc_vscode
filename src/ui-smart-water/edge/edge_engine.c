// ========== edge_engine.c ==========
// 边缘端数据预处理引擎主体。
//
//  Linux/ARM: Paho 回调线程 push → 队列(condvar) → worker 线程消费
//  PC/Windows: push → 直接同步 process_sample (无队列, 便于演示)
//
//  worker 职责: 去重 → 异常分类 → 写 CSV → 更新快照/历史环/日计数 → 周期(60s)分析
//  LVGL 通过 get_* 短锁拷出, 不阻塞 UI。
#include "edge_engine.h"
#include "edge_store.h"
#include "edge_analysis.h"
#include "sensor_range.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ===== 量程表 (sensor_range.h 声明为 extern) ===== */
const sensor_phys_t g_sensor_phys[SENSOR_IDX_COUNT] = {
    [SENSOR_IDX_TEMP]  = { "温度",   "°C",   0, 40,   5.0f,  1 },
    [SENSOR_IDX_HUMI]  = { "湿度",   "%",    0, 100,  20.0f, 1 },
    [SENSOR_IDX_LIGHT] = { "光照",   "%",    0, 100,  60.0f, 0 },
    [SENSOR_IDX_DO]    = { "溶解氧", "mg/L", 0, 20,   3.0f,  1 },
    [SENSOR_IDX_PH]    = { "pH值",   "",     0, 14,   1.0f,  2 },
    [SENSOR_IDX_NH3N]  = { "氨氮",   "mg/L", 0, 5,    0.5f,  2 },
};

/* ===== 互斥可移植宏 =====
 *  Linux: pthread_mutex_t; PC: 无 pthread, 用 int 占位(锁操作为空)。 */
#ifdef __linux__
  #include <pthread.h>
  typedef pthread_mutex_t edge_mtx_t;
  #define MTX_INIT(m)   pthread_mutex_init(&(m), NULL)
  #define MTX_LOCK(m)   pthread_mutex_lock(&(m))
  #define MTX_UNLOCK(m) pthread_mutex_unlock(&(m))
#else
  typedef int edge_mtx_t;
  #define MTX_INIT(m)   ((void)0)
  #define MTX_LOCK(m)   ((void)0)
  #define MTX_UNLOCK(m) ((void)0)
#endif

/* ===== 内部状态 ===== */
typedef struct { float value; uint32_t ts; bool valid; } latest_t;

static latest_t          g_latest[SENSOR_IDX_COUNT];   /* 每路最近有效值 */
static float             g_ring[SENSOR_IDX_COUNT][EDGE_RING_CAP];
static int               g_ring_n, g_ring_head;
static edge_analysis_t   g_analysis;                   /* 分析缓存 */
static raw_sample_t      g_last_valid;                 /* 去重/突变比较基准 */
static bool              g_have_last = false;

static bool g_run = false;
static bool g_inline_mode = false;   /* true: push 内联处理 (PC 或 worker 失败) */

#ifdef __linux__
  #define QUEUE_CAP 256
  static raw_sample_t    g_queue[QUEUE_CAP];
  static int             g_q_head, g_q_tail, g_q_count;
  static pthread_mutex_t g_q_mtx;
  static pthread_cond_t  g_q_cv;
  static pthread_t       g_worker;
#endif

static edge_mtx_t g_latest_mtx, g_ring_mtx, g_analysis_mtx;

/* ===== 清洗分类 =====
 *  返回 EDGE_FLAG_*。原始数据全部落盘, 仅 VALID 进环+分析。 */
static int classify(const raw_sample_t * s)
{
    if(g_have_last) {
        if(s->seq >= 0 && s->seq == g_last_valid.seq) return EDGE_FLAG_DUPE;
        bool same = true;
        for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
            if(fabsf(s->values[i] - g_last_valid.values[i]) > 1e-3f) { same = false; break; }
        }
        if(same) return EDGE_FLAG_DUPE;
    }
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        const sensor_phys_t * p = &g_sensor_phys[i];
        float v = s->values[i];
        if(v < p->phys_min || v > p->phys_max) return EDGE_FLAG_RANGE;
        if(g_have_last && fabsf(v - g_last_valid.values[i]) > p->max_delta) return EDGE_FLAG_SPIKE;
    }
    return EDGE_FLAG_VALID;
}

/* ===== 周期分析: 从 CSV 取最近 N 条有效样本 → 回归 + 相关 + 日汇总 ===== */
#define ANALYSIS_N         600     /* ≈30min @3s/条 */
#define SAMPLES_PER_HOUR   1200    /* 3600/3 */

static void recompute_analysis(uint32_t now_ts)
{
    edge_analysis_t a;
    memset(&a, 0, sizeof(a));
    a.ts = now_ts;

    raw_sample_t * buf = (raw_sample_t *)malloc(sizeof(raw_sample_t) * ANALYSIS_N);
    int n = buf ? store_load_recent_valid(buf, ANALYSIS_N) : 0;

    if(n >= 2) {
        float * y = (float *)malloc(sizeof(float) * (size_t)n);
        /* 每路回归 */
        for(int i = 0; i < SENSOR_IDX_COUNT && y; i++) {
            for(int k = 0; k < n; k++) y[k] = buf[k].values[i];
            bool ok = false; float sl = 0, ic = 0;
            analysis_regression(y, n, &sl, &ic, &ok);
            if(ok) {
                a.regr[i].slope = sl;
                a.regr[i].intercept = ic;
                a.regr[i].cur = buf[n - 1].values[i];
                a.regr[i].pred_1h = ic + sl * (float)(n + SAMPLES_PER_HOUR);
                /* 1h 外推两点约束:
                 *  1) 偏离当前值不超过量程 20% — 防短数据/噪声斜率被 1200 步外推放大到边界
                 *  2) 钳制到物理量程 — 防负值 / >100% 等物理不可能值 */
                {
                    const sensor_phys_t * p = &g_sensor_phys[i];
                    float cur = a.regr[i].cur;
                    float max_dev = (p->phys_max - p->phys_min) * 0.20f;
                    float pred = a.regr[i].pred_1h;
                    if(pred > cur + max_dev) pred = cur + max_dev;
                    if(pred < cur - max_dev) pred = cur - max_dev;
                    if(pred < p->phys_min) pred = p->phys_min;
                    if(pred > p->phys_max) pred = p->phys_max;
                    a.regr[i].pred_1h = pred;
                }
                a.regr[i].valid = true;
            }
        }
        /* 两两 Pearson, 收集 |r|>=0.5 */
        float * ya = (float *)malloc(sizeof(float) * (size_t)n);
        float * yb = (float *)malloc(sizeof(float) * (size_t)n);
        corr_pair_t pairs[EDGE_CORR_MAX];
        int pn = 0;
        if(ya && yb) {
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
                for(int j = i + 1; j < SENSOR_IDX_COUNT; j++) {
                    for(int k = 0; k < n; k++) { ya[k] = buf[k].values[i]; yb[k] = buf[k].values[j]; }
                    bool ok = false;
                    float r = analysis_correlation(ya, yb, n, &ok);
                    if(ok && fabsf(r) >= 0.5f && pn < EDGE_CORR_MAX) {
                        pairs[pn].a = (int8_t)i; pairs[pn].b = (int8_t)j; pairs[pn].r = r; pn++;
                    }
                }
            }
        }
        free(ya); free(yb);
        /* 按 |r| 降序 (插入排序, n≤15) */
        for(int i = 1; i < pn; i++) {
            corr_pair_t key = pairs[i]; int j = i - 1;
            while(j >= 0 && fabsf(pairs[j].r) < fabsf(key.r)) { pairs[j+1] = pairs[j]; j--; }
            pairs[j+1] = key;
        }
        a.corr_n = pn;
        memcpy(a.corr, pairs, sizeof(corr_pair_t) * (size_t)pn);
        free(y);
    }
    free(buf);

    /* 日报: 扫当日 CSV → 跨重启正确, 不依赖内存计数器 */
    store_compute_daily(a.daily);
    a.valid = true;

    MTX_LOCK(g_analysis_mtx);
    g_analysis = a;
    MTX_UNLOCK(g_analysis_mtx);
    store_save_analysis(&a);
}

/* ===== 处理一帧 (worker / 内联共用) ===== */
static void process_sample(const raw_sample_t * s)
{
    int flag = classify(s);
    store_append_raw(s, flag);

    if(flag == EDGE_FLAG_VALID) {
        MTX_LOCK(g_latest_mtx);
        for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
            g_latest[i].value = s->values[i];
            g_latest[i].ts = s->ts;
            g_latest[i].valid = true;
        }
        MTX_UNLOCK(g_latest_mtx);

        MTX_LOCK(g_ring_mtx);
        for(int i = 0; i < SENSOR_IDX_COUNT; i++) g_ring[i][g_ring_head] = s->values[i];
        g_ring_head = (g_ring_head + 1) % EDGE_RING_CAP;
        if(g_ring_n < EDGE_RING_CAP) g_ring_n++;
        MTX_UNLOCK(g_ring_mtx);

        g_last_valid = *s;
        g_have_last = true;
    }

    /* 周期分析 (每 60s, 首帧立即跑一次) — 内部扫当日 CSV 算日报, 跨重启正确 */
    static uint32_t last_analysis = 0;
    if(last_analysis == 0 || s->ts >= last_analysis + 60) {
        last_analysis = s->ts;
        recompute_analysis(s->ts);
    }
}

#ifdef __linux__
static void * worker_main(void * arg)
{
    (void)arg;
    for(;;) {
        raw_sample_t s;
        pthread_mutex_lock(&g_q_mtx);
        while(g_q_count == 0 && g_run) pthread_cond_wait(&g_q_cv, &g_q_mtx);
        if(!g_run) { pthread_mutex_unlock(&g_q_mtx); break; }
        s = g_queue[g_q_tail];
        g_q_tail = (g_q_tail + 1) % QUEUE_CAP;
        g_q_count--;
        pthread_mutex_unlock(&g_q_mtx);
        process_sample(&s);
    }
    return NULL;
}
#endif

/* ===== 公共 API ===== */

bool edge_engine_init(void)
{
    store_ensure_dirs();
    MTX_INIT(g_latest_mtx); MTX_INIT(g_ring_mtx); MTX_INIT(g_analysis_mtx);
#ifdef __linux__
    MTX_INIT(g_q_mtx);
    pthread_cond_init(&g_q_cv, NULL);
#endif
    g_run = true;
    g_inline_mode = false;
    g_ring_n = g_ring_head = 0;
    g_have_last = false;

    /* 启动重建: 从今日 CSV 回放有效样本 → 填充 ring + latest + last_valid */
    raw_sample_t * buf = (raw_sample_t *)malloc(sizeof(raw_sample_t) * EDGE_RING_CAP);
    if(buf) {
        int n = store_load_tail_samples(buf, EDGE_RING_CAP);
        for(int k = 0; k < n; k++) {
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) g_ring[i][g_ring_head] = buf[k].values[i];
            g_ring_head = (g_ring_head + 1) % EDGE_RING_CAP;
            if(g_ring_n < EDGE_RING_CAP) g_ring_n++;
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
                g_latest[i].value = buf[k].values[i];
                g_latest[i].ts = buf[k].ts;
                g_latest[i].valid = true;
            }
            g_last_valid = buf[k];
            g_have_last = true;
        }
        free(buf);
        printf("[edge] rebuilt ring: %d samples from %s\n", n, store_today_path());
    }

    /* 加载分析缓存 */
    edge_analysis_t a;
    if(store_load_analysis(&a)) {
        MTX_LOCK(g_analysis_mtx);
        g_analysis = a;
        MTX_UNLOCK(g_analysis_mtx);
        printf("[edge] loaded analysis cache (ts=%u)\n", (unsigned)a.ts);
    }

#ifdef __linux__
    if(pthread_create(&g_worker, NULL, worker_main, NULL) != 0) {
        printf("[edge] worker create failed → inline mode\n");
        g_inline_mode = true;
    } else {
        pthread_detach(g_worker);
    }
#else
    g_inline_mode = true;   /* PC: 无 worker, push 内联 */
#endif
    printf("[edge] engine started (%s mode)\n", g_inline_mode ? "inline" : "worker-thread");
    return true;
}

void edge_engine_deinit(void)
{
    g_run = false;
#ifdef __linux__
    if(!g_inline_mode) {
        pthread_mutex_lock(&g_q_mtx);
        pthread_cond_broadcast(&g_q_cv);
        pthread_mutex_unlock(&g_q_mtx);
    }
#endif
    /* 落最后一次分析 */
    edge_analysis_t a;
    if(edge_engine_get_analysis(&a)) store_save_analysis(&a);
}

void edge_engine_push(const raw_sample_t * s)
{
    if(s == NULL || !g_run) return;
#ifdef __linux__
    if(!g_inline_mode) {
        pthread_mutex_lock(&g_q_mtx);
        g_queue[g_q_head] = *s;
        g_q_head = (g_q_head + 1) % QUEUE_CAP;
        if(g_q_count < QUEUE_CAP) g_q_count++;
        else g_q_tail = (g_q_tail + 1) % QUEUE_CAP;   /* 满则丢最旧 */
        pthread_cond_signal(&g_q_cv);
        pthread_mutex_unlock(&g_q_mtx);
        return;
    }
#endif
    process_sample(s);
}

bool edge_engine_get_latest(sensor_idx_t idx, float * value, uint32_t * ts)
{
    if(idx < 0 || idx >= SENSOR_IDX_COUNT) return false;
    MTX_LOCK(g_latest_mtx);
    bool ok = g_latest[idx].valid;
    if(ok) {
        if(value) *value = g_latest[idx].value;
        if(ts)    *ts    = g_latest[idx].ts;
    }
    MTX_UNLOCK(g_latest_mtx);
    return ok;
}

int edge_engine_get_history(sensor_idx_t idx, float * buf, int max)
{
    if(idx < 0 || idx >= SENSOR_IDX_COUNT || buf == NULL || max <= 0) return 0;
    MTX_LOCK(g_ring_mtx);
    int total = g_ring_n;
    int count = total < max ? total : max;
    int start = (g_ring_head - count + EDGE_RING_CAP) % EDGE_RING_CAP;
    for(int i = 0; i < count; i++) {
        buf[i] = g_ring[idx][(start + i) % EDGE_RING_CAP];
    }
    MTX_UNLOCK(g_ring_mtx);
    return count;
}

bool edge_engine_get_analysis(edge_analysis_t * out)
{
    if(out == NULL) return false;
    MTX_LOCK(g_analysis_mtx);
    bool ok = g_analysis.valid;
    if(ok) *out = g_analysis;
    MTX_UNLOCK(g_analysis_mtx);
    return ok;
}

/* ===== PC 模拟喂料 (复刻 mqtt_pub_demo 随机逻辑) ===== */
void edge_engine_sim_feed(void)
{
    /* 平滑随机游走 (PC 演示): 每帧小幅扰动 + 钳制到工作区间,
     * 偶发真实突变演示 flag=2 检测。
     * 旧实现每帧独立均匀随机 → 相邻帧差几乎必超 max_delta → 75% 误判突变,
     * 当日报表"剔除"动辄上百。改成游走后剔除数回到个位。 */
    static uint32_t rnd = 0;
    static int32_t  seq = 0;
    static float    prev[SENSOR_IDX_COUNT];
    static bool     inited = false;
    if(rnd == 0) rnd = (uint32_t)time(NULL);
    if(!inited) {
        if(g_have_last) {   /* 从 CSV 重建后无缝衔接, 避免首帧误判 */
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) prev[i] = g_last_valid.values[i];
        } else {
            prev[SENSOR_IDX_TEMP]  = 26.0f;
            prev[SENSOR_IDX_HUMI]  = 68.0f;
            prev[SENSOR_IDX_LIGHT] = 55.0f;
            prev[SENSOR_IDX_DO]    = 6.5f;
            prev[SENSOR_IDX_PH]    = 7.2f;
            prev[SENSOR_IDX_NH3N]  = 0.25f;
        }
        inited = true;
    }

    /* 工作区间 (比 phys 窄, 贴近真实养殖) + 游走步长 (<< max_delta, 不触发突变) */
    static const struct { float lo, hi, step; } rng[SENSOR_IDX_COUNT] = {
        [SENSOR_IDX_TEMP]  = { 24.0f, 28.0f, 0.4f  },
        [SENSOR_IDX_HUMI]  = { 60.0f, 80.0f, 2.0f  },
        [SENSOR_IDX_LIGHT] = { 30.0f, 85.0f, 5.0f  },
        [SENSOR_IDX_DO]    = { 5.5f,  8.0f, 0.2f  },
        [SENSOR_IDX_PH]    = { 6.8f,  7.6f, 0.05f },
        [SENSOR_IDX_NH3N]  = { 0.10f, 0.45f, 0.03f },
    };

    raw_sample_t s;
    s.ts  = (uint32_t)time(NULL);
    s.seq = seq++;

    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        rnd = rnd * 1103515245u + 12345u;
        int32_t r = (int32_t)((rnd / 65536u) % 2000u) - 1000;        /* -1000..+1000 */
        float v = prev[i] + (float)r / 1000.0f * rng[i].step;        /* ±step */
        if(v < rng[i].lo) v = rng[i].lo;
        if(v > rng[i].hi) v = rng[i].hi;
        prev[i] = v;
        s.values[i] = v;
    }

    /* 偶发真实突变 (≈1/60): 仅选 max_delta 较小、可安全超量程内跳变的通道
     * (光照 max_delta=60 vs 量程 100, 中点无法在量程内突变, 故排除)。
     * prev 不更新 → 下帧从正常值继续, 避免连锁突变。 */
    rnd = rnd * 1103515245u + 12345u;
    if((rnd % 60) == 0) {
        static const sensor_idx_t spike_ch[] = {
            SENSOR_IDX_TEMP, SENSOR_IDX_DO, SENSOR_IDX_PH, SENSOR_IDX_NH3N };
        sensor_idx_t ch = spike_ch[(rnd / 60u) % 4];
        float mag = g_sensor_phys[ch].max_delta * 1.8f;   /* > max_delta → 突变 */
        float v = prev[ch] + mag;
        if(v > g_sensor_phys[ch].phys_max) v = prev[ch] - mag;
        s.values[ch] = v;   /* prev[ch] 保持不变 */
    }

    edge_engine_push(&s);
}
