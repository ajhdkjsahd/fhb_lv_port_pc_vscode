// ========== edge_store.c ==========
// 存储层实现: CSV 平铺文件 + 纯文本分析缓存。
//  - CSV:   raw/YYYY-MM-DD.csv, 列 ts,seq,temp,humi,light,do,ph,nh3n,flag
//  - 缓存:  analysis/latest.txt, 行式键值, sscanf 解析
//
// 刻意不引 cJSON, 保持 edge/ 零依赖 → PC(USE_MQTT=OFF) 也能编译。
// 路径: Linux /root/aqua_data (eMMC), PC ./aqua_data。
#include "edge_store.h"
#include "sensor_range.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
  #include <sys/stat.h>
  #include <unistd.h>
  #define MAKE_DIR(p) mkdir((p), 0755)
  #define LOCAL_TIME(t, tm) localtime_r((t), (tm))
#else
  #include <sys/stat.h>
  #include <direct.h>
  #define MAKE_DIR(p) _mkdir((p))
  /* PC 单线程桩, localtime 静态缓冲无竞争 */
  static void LOCAL_TIME(const time_t * t, struct tm * out) { *out = *localtime(t); }
#endif

#define EDGE_FSYNC_EVERY 10          /* 每 10 条 fsync 一次 (≈30s 持久化) */

#ifdef __linux__
#define DATA_ROOT "/root/aqua_data"
#else
#define DATA_ROOT "aqua_data"
#endif

/* ── 目录与路径 ── */

void store_ensure_dirs(void)
{
    char p[256];
    snprintf(p, sizeof(p), "%s", DATA_ROOT);          MAKE_DIR(p);
    snprintf(p, sizeof(p), "%s/raw", DATA_ROOT);      MAKE_DIR(p);
    snprintf(p, sizeof(p), "%s/analysis", DATA_ROOT); MAKE_DIR(p);
    /* MAKE_DIR 在目录已存在时返回 -1, 忽略即可 */
}

static void now_ymd(int * y, int * m, int * d)
{
    time_t t = time(NULL);
    struct tm tmv;
    LOCAL_TIME(&t, &tmv);
    *y = tmv.tm_year + 1900;
    *m = tmv.tm_mon + 1;
    *d = tmv.tm_mday;
}

const char * store_today_path(void)
{
    static char buf[256];
    int y, m, d;
    now_ymd(&y, &m, &d);
    snprintf(buf, sizeof(buf), "%s/raw/%04d-%02d-%02d.csv", DATA_ROOT, y, m, d);
    return buf;
}

static const char * store_analysis_path(void)
{
    static char buf[256];
    snprintf(buf, sizeof(buf), "%s/analysis/latest.txt", DATA_ROOT);
    return buf;
}

/* ── 写: 追加一帧 ── */

static int g_pending_writes = 0;

void store_append_raw(const raw_sample_t * s, int flag)
{
    if(s == NULL) return;
    FILE * f = fopen(store_today_path(), "a");
    if(f == NULL) return;
    fprintf(f, "%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n",
            (unsigned)s->ts, (int)s->seq,
            s->values[0], s->values[1], s->values[2],
            s->values[3], s->values[4], s->values[5], flag);
    g_pending_writes++;
    if(g_pending_writes >= EDGE_FSYNC_EVERY) {
#ifdef __linux__
        fflush(f);
        fsync(fileno(f));
#endif
        g_pending_writes = 0;
    }
    fclose(f);   /* 每次开关文件: 简单且崩溃安全, 3s/条开销可忽略 */
}

/* ── 读: 共享的「尾部 N 条有效样本」加载 ──
 *  用环形缓冲只保留最后 max 条, 无需把整文件读进内存。 */
static int load_last_valid(raw_sample_t * out, int max)
{
    if(out == NULL || max <= 0) return 0;
    FILE * f = fopen(store_today_path(), "r");
    if(f == NULL) return 0;

    raw_sample_t * ring = (raw_sample_t *)malloc(sizeof(raw_sample_t) * (size_t)max);
    if(ring == NULL) { fclose(f); return 0; }

    int n = 0, head = 0;
    char line[256];
    while(fgets(line, sizeof(line), f)) {
        raw_sample_t s;
        int flag;
        /* ts,seq,v0..v5,flag */
        int got = sscanf(line, "%u,%d,%f,%f,%f,%f,%f,%f,%d",
                         (unsigned *)&s.ts, &s.seq,
                         &s.values[0], &s.values[1], &s.values[2],
                         &s.values[3], &s.values[4], &s.values[5], &flag);
        if(got != 9) continue;
        if(flag != EDGE_FLAG_VALID) continue;   /* 仅回放有效样本 */
        ring[head] = s;
        head = (head + 1) % max;
        if(n < max) n++;
    }
    fclose(f);

    int start = (n < max) ? 0 : head;          /* 满环时 head 指向最旧 */
    for(int i = 0; i < n; i++) {
        out[i] = ring[(start + i) % max];
    }
    free(ring);
    return n;
}

int store_load_tail_samples(raw_sample_t * out, int max)
{
    return load_last_valid(out, max);
}

int store_load_recent_valid(raw_sample_t * out, int max)
{
    return load_last_valid(out, max);
}

/* 扫描今日 CSV 全量, 算每路 min/max/sum/count + 全局 reject 计数。 */
void store_compute_daily(daily_t out[SENSOR_IDX_COUNT])
{
    if(out == NULL) return;
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        out[i].min = 1e9f; out[i].max = -1e9f;
        out[i].mean = 0.0f; out[i].count = 0; out[i].reject = 0; out[i].valid = false;
    }
    FILE * f = fopen(store_today_path(), "r");
    if(f == NULL) return;

    double sum[SENSOR_IDX_COUNT];
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) sum[i] = 0.0;

    char line[256];
    while(fgets(line, sizeof(line), f)) {
        raw_sample_t s;
        int flag;
        int got = sscanf(line, "%u,%d,%f,%f,%f,%f,%f,%f,%d",
                         (unsigned *)&s.ts, &s.seq,
                         &s.values[0], &s.values[1], &s.values[2],
                         &s.values[3], &s.values[4], &s.values[5], &flag);
        if(got != 9) continue;
        if(flag == EDGE_FLAG_VALID) {
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
                float v = s.values[i];
                if(v < out[i].min) out[i].min = v;
                if(v > out[i].max) out[i].max = v;
                sum[i] += v;
                out[i].count++;
                out[i].valid = true;
            }
        } else {
            /* 整帧剔除 → 6 路 reject 各 +1 (与引擎运行计数口径一致) */
            for(int i = 0; i < SENSOR_IDX_COUNT; i++) out[i].reject++;
        }
    }
    fclose(f);
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        if(out[i].count > 0) out[i].mean = (float)(sum[i] / out[i].count);
        else { out[i].min = 0; out[i].max = 0; out[i].valid = false; }
    }
}

/* ── 分析缓存 (纯文本, sscanf 解析) ──
 *  格式:
 *    ts <uint>
 *    regr <idx> <slope> <intercept> <cur> <pred_1h> <0|1>          ×6
 *    corr_n <n>
 *    corr <a> <b> <r>                                                ×corr_n
 *    daily <idx> <min> <max> <mean> <count> <reject> <0|1>          ×6
 */

bool store_load_analysis(edge_analysis_t * out)
{
    if(out == NULL) return false;
    memset(out, 0, sizeof(*out));
    FILE * f = fopen(store_analysis_path(), "r");
    if(f == NULL) return false;

    char line[256];
    bool got_ts = false;
    while(fgets(line, sizeof(line), f)) {
        if(strncmp(line, "ts ", 3) == 0) {
            unsigned ts;
            if(sscanf(line + 3, "%u", &ts) == 1) { out->ts = ts; got_ts = true; }
        } else if(strncmp(line, "regr ", 5) == 0) {
            int idx, valid;
            float sl, ic, cur, pred;
            if(sscanf(line + 5, "%d %f %f %f %f %d",
                      &idx, &sl, &ic, &cur, &pred, &valid) == 6 &&
               idx >= 0 && idx < SENSOR_IDX_COUNT) {
                out->regr[idx].slope = sl;
                out->regr[idx].intercept = ic;
                out->regr[idx].cur = cur;
                out->regr[idx].pred_1h = pred;
                out->regr[idx].valid = valid != 0;
            }
        } else if(strncmp(line, "corr_n ", 7) == 0) {
            sscanf(line + 7, "%d", &out->corr_n);
            if(out->corr_n < 0) out->corr_n = 0;
            if(out->corr_n > EDGE_CORR_MAX) out->corr_n = EDGE_CORR_MAX;
        } else if(strncmp(line, "corr ", 5) == 0) {
            int a, b; float r;
            if(sscanf(line + 5, "%d %d %f", &a, &b, &r) == 3 &&
               out->corr_n < EDGE_CORR_MAX) {
                out->corr[out->corr_n].a = (int8_t)a;
                out->corr[out->corr_n].b = (int8_t)b;
                out->corr[out->corr_n].r = r;
                out->corr_n++;
            }
        } else if(strncmp(line, "daily ", 6) == 0) {
            int idx, valid, count, reject;
            float mn, mx, mean;
            if(sscanf(line + 6, "%d %f %f %f %d %d %d",
                      &idx, &mn, &mx, &mean, &count, &reject, &valid) == 7 &&
               idx >= 0 && idx < SENSOR_IDX_COUNT) {
                out->daily[idx].min = mn;
                out->daily[idx].max = mx;
                out->daily[idx].mean = mean;
                out->daily[idx].count = count;
                out->daily[idx].reject = reject;
                out->daily[idx].valid = valid != 0;
            }
        }
    }
    fclose(f);
    out->valid = got_ts;
    return got_ts;
}

void store_save_analysis(const edge_analysis_t * in)
{
    if(in == NULL) return;
    FILE * f = fopen(store_analysis_path(), "w");
    if(f == NULL) return;
    fprintf(f, "ts %u\n", (unsigned)in->ts);
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        const regr_t * r = &in->regr[i];
        fprintf(f, "regr %d %.4f %.4f %.4f %.4f %d\n", i,
                r->slope, r->intercept, r->cur, r->pred_1h, r->valid ? 1 : 0);
    }
    fprintf(f, "corr_n %d\n", in->corr_n);
    for(int i = 0; i < in->corr_n; i++) {
        fprintf(f, "corr %d %d %.4f\n",
                (int)in->corr[i].a, (int)in->corr[i].b, in->corr[i].r);
    }
    for(int i = 0; i < SENSOR_IDX_COUNT; i++) {
        const daily_t * d = &in->daily[i];
        fprintf(f, "daily %d %.3f %.3f %.3f %d %d %d\n", i,
                d->min, d->max, d->mean, d->count, d->reject, d->valid ? 1 : 0);
    }
    fclose(f);
}
