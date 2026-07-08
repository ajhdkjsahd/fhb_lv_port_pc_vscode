// ========== ai_tools.c ==========
// 工具注册表 + 硬件实现 — 把 Qwen2.5 的 tool_calls 转成真硬件动作
//
//   handler 全部转调两层真实硬件:
//     1. ai_hardware.c  — 板卡实测的 LED sysfs / GPIO28 按键 / GPIO78 蜂鸣器
//                         / MMA8653 input 子系统 / 智能随机开灯
//     2. edge_engine    — 6 路水质传感器 (温/湿/光/溶氧/pH/氨氮) 最新快照
//
//   新增硬件步骤:
//     1. 实现 handler (签名: char* foo(const cJSON* args))
//     2. 在 g_tools[] 加一行: {name, desc, schema, handler}
//   模型立即就能用, 不需要改 agent 一行代码。
#include "ai_tools.h"
#include "ai_hardware.h"           /* 板卡硬件: ai_execute_action */
#include "../../edge/edge_engine.h"   /* 水质传感器快照 (含 sensor_idx_t) */
#include "../app_actions.h"           /* 闭环控制配置读取 (app_action_control_get_*) */

#include "lvgl/lvgl.h"   /* LV_LOG_USER */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 辅助
 * ============================================================ */

/* 按传感器定义的小数位四舍五入 (与 sensor_page.c 显示一致) */
static double round_sensor_val(sensor_idx_t idx, float v)
{
    int dec = g_sensor_phys[idx].dec;
    double scale = 1.0;
    for (int i = 0; i < dec; i++) scale *= 10.0;
    return (double)((int)((double)v * scale + 0.5)) / scale;
}

/* 把 ai_hardware 返回的中文串包成 JSON
 *   "[错误]" 开头 → ok:false, 否则 ok:true */
static char* wrap_hw_result(const char *hw_result)
{
    if (!hw_result) {
        return strdup("{\"ok\":false,\"msg\":\"硬件无返回\"}");
    }
    cJSON *o = cJSON_CreateObject();
    int ok = (strncmp(hw_result, "[错误]", 7) != 0);
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "msg", hw_result);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* 辅助: 安全取字符串参数 */
static const char* arg_str(const cJSON *args, const char *key)
{
    cJSON *it = cJSON_GetObjectItem(args, key);
    return (it && cJSON_IsString(it)) ? it->valuestring : NULL;
}

/* 辅助: 安全取整数参数 */
static int arg_int(const cJSON *args, const char *key, int def)
{
    cJSON *it = cJSON_GetObjectItem(args, key);
    return (it && cJSON_IsNumber(it)) ? it->valueint : def;
}

/* ============================================================
 * 工具 handler
 * ============================================================ */

/* 控制 1~4 号 LED (D7~D10) */
static char* tool_control_led(const cJSON *args)
{
    int led_id = arg_int(args, "led_id", 0);
    const char *state = arg_str(args, "state");

    if (led_id < 1 || led_id > 4 || !state) {
        return strdup("{\"ok\":false,\"msg\":\"参数无效: led_id 需 1-4, state 需 on/off\"}");
    }

    char action[32];
    snprintf(action, sizeof(action), "led%d_%s", led_id,
             (strcmp(state, "on") == 0) ? "on" : "off");
    char *hw = ai_execute_action(action);
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 全部 LED 开/关 */
static char* tool_control_all_leds(const cJSON *args)
{
    const char *state = arg_str(args, "state");
    if (!state) {
        return strdup("{\"ok\":false,\"msg\":\"参数无效: state 需 on/off\"}");
    }
    const char *action = (strcmp(state, "on") == 0) ? "led_all_on" : "led_all_off";
    char *hw = ai_execute_action(action);
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 随机点亮一盏当前灭着的 LED (全亮则全灭) */
static char* tool_random_led_on(const cJSON *args)
{
    (void)args;
    char *hw = ai_execute_action("led_random_on");
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 蜂鸣器开/关 */
static char* tool_control_buzzer(const cJSON *args)
{
    const char *state = arg_str(args, "state");
    if (!state) {
        return strdup("{\"ok\":false,\"msg\":\"参数无效: state 需 on/off\"}");
    }
    const char *action = (strcmp(state, "on") == 0) ? "buzzer_on" : "buzzer_off";
    char *hw = ai_execute_action(action);
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 蜂鸣器反转 */
static char* tool_toggle_buzzer(const cJSON *args)
{
    (void)args;
    char *hw = ai_execute_action("buzzer_toggle");
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 读 K2 按键 (GPIO28, 0=按下) */
static char* tool_read_button(const cJSON *args)
{
    (void)args;
    char *hw = ai_execute_action("read_button");
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 读 MMA8653 三轴加速度 */
static char* tool_read_acceleration(const cJSON *args)
{
    (void)args;
    char *hw = ai_execute_action("read_accel");
    char *out = wrap_hw_result(hw);
    free(hw);
    return out;
}

/* 读 6 路水质传感器之一 (来自边缘引擎快照) */
static char* tool_read_water_sensor(const cJSON *args)
{
    const char *name = arg_str(args, "sensor");
    if (!name) {
        return strdup("{\"ok\":false,\"msg\":\"参数无效: sensor 需 "
                      "temperature/humidity/light/do/ph/nh3n\"}");
    }

    sensor_idx_t idx;
    const char *unit;
    if      (strcmp(name, "temperature") == 0) { idx = SENSOR_IDX_TEMP;  unit = "C";    }
    else if (strcmp(name, "humidity")    == 0) { idx = SENSOR_IDX_HUMI;  unit = "%";    }
    else if (strcmp(name, "light")       == 0) { idx = SENSOR_IDX_LIGHT; unit = "%";    }
    else if (strcmp(name, "do")          == 0) { idx = SENSOR_IDX_DO;    unit = "mg/L"; }
    else if (strcmp(name, "ph")          == 0) { idx = SENSOR_IDX_PH;    unit = "";     }
    else if (strcmp(name, "nh3n")        == 0) { idx = SENSOR_IDX_NH3N;  unit = "mg/L"; }
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "未知传感器: %s", name);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", false);
        cJSON_AddStringToObject(o, "msg", msg);
        char *s = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        return s;
    }

    float value = 0.0f;
    uint32_t ts = 0;
    bool ok = edge_engine_get_latest(idx, &value, &ts);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", ok);
    cJSON_AddStringToObject(o, "sensor", name);
    if (ok) {
        /* 按传感器定义的小数位四舍五入, 与传感器页面/趋势报表显示一致 */
        cJSON_AddNumberToObject(o, "value", round_sensor_val(idx, value));
        cJSON_AddStringToObject(o, "unit", unit);
        cJSON_AddNumberToObject(o, "timestamp", (double)ts);
        cJSON_AddStringToObject(o, "msg", "读取成功");
    } else {
        cJSON_AddStringToObject(o, "msg", "传感器暂无数据 (引擎未收到该路采样)");
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* ── 传感器名称/单位表 (与 sensor_idx_t 顺序一致) ── */
static const char * s_names[SENSOR_IDX_COUNT] = {
    "temperature", "humidity", "light", "do", "ph", "nh3n"
};
static const char * s_units[SENSOR_IDX_COUNT] = {
    "°C", "%", "%", "mg/L", "", "mg/L"
};

/* 一次性读取全部 6 路水质传感器 + 当日汇总, 用于全面环境评价 */
static char* tool_read_water_quality(const cJSON *args)
{
    (void)args;

    edge_analysis_t an;
    bool has_an = edge_engine_get_analysis(&an);

    cJSON *o  = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    int ok_count = 0;
    for (int i = 0; i < SENSOR_IDX_COUNT; i++) {
        float val = 0.0f;
        uint32_t ts = 0;
        sensor_idx_t idx = (sensor_idx_t)i;
        bool ok = edge_engine_get_latest(idx, &val, &ts);

        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "sensor", s_names[i]);
        cJSON_AddStringToObject(s, "unit",   s_units[i]);
        cJSON_AddBoolToObject(s, "available", ok);
        if (ok) {
            /* 按传感器定义的小数位四舍五入, 与传感器页面/趋势报表一致 */
            cJSON_AddNumberToObject(s, "value", round_sensor_val(idx, val));
            cJSON_AddNumberToObject(s, "timestamp", (double)ts);
            ok_count++;
        }
        /* 附加当日汇总 (min/max/mean/reject), 同样四舍五入 */
        if (has_an && an.daily[i].valid) {
            cJSON_AddNumberToObject(s, "today_min",    round_sensor_val(idx, an.daily[i].min));
            cJSON_AddNumberToObject(s, "today_max",    round_sensor_val(idx, an.daily[i].max));
            cJSON_AddNumberToObject(s, "today_mean",   round_sensor_val(idx, an.daily[i].mean));
            cJSON_AddNumberToObject(s, "reject_count", an.daily[i].reject);
        }
        cJSON_AddItemToArray(arr, s);
    }

    cJSON_AddBoolToObject(o, "ok", (ok_count > 0));
    cJSON_AddNumberToObject(o, "sensors_available", ok_count);
    cJSON_AddItemToObject(o, "readings", arr);

    /* 附上水产养殖参考区间, 模型可据此判断水质是否健康 */
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "temperature", "22~30°C (温水鱼), <20°C 生长慢, >32°C 缺氧风险");
    cJSON_AddStringToObject(ref, "humidity",    "50~85% (环境湿度, 非关键指标)");
    cJSON_AddStringToObject(ref, "light",       "40~80% (日间光照)");
    cJSON_AddStringToObject(ref, "do",          ">5 mg/L 优良, 3~5 mg/L 偏低, <3 mg/L 危险 (缺氧)");
    cJSON_AddStringToObject(ref, "ph",          "6.5~8.5 安全, 7.0~8.0 理想");
    cJSON_AddStringToObject(ref, "nh3n",        "<0.5 mg/L 安全, 0.5~1.0 mg/L 偏高, >1.0 mg/L 有毒");
    cJSON_AddItemToObject(o, "reference_ranges", ref);

    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* 读取边缘引擎分析结果: 回归趋势 + 1h 预测 + 当日统计 + 传感器相关性,
 * 用于智能推荐投喂量/增氧时长等最优方案, 以及故障趋势预判 */
static char* tool_analyze_environment(const cJSON *args)
{
    (void)args;

    edge_analysis_t an;
    if (!edge_engine_get_analysis(&an) || !an.valid) {
        return strdup("{\"ok\":false,\"msg\":\"分析数据不可用 (引擎尚未产出分析结果, "
                       "可能需要积累至少 20 个采样点)。请先调用 read_water_quality 获取即时值。\"}");
    }

    cJSON *o  = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);

    /* ── 每路回归 + 预测 ──
     *   实时值 (current) 从 edge_engine_get_latest 读 — 保证与传感器页面一致。
     *   趋势/预测/当日汇总 从分析缓存读 — 基于历史数据计算, 略滞后但稳定。 */
    cJSON *trends = cJSON_CreateArray();
    for (int i = 0; i < SENSOR_IDX_COUNT; i++) {
        if (!an.regr[i].valid) continue;
        sensor_idx_t idx = (sensor_idx_t)i;

        /* 实时最新值: 与传感器页面同一数据源 (引擎快照) */
        float latest_val = an.regr[i].cur;   /* 兜底: 用分析缓存的值 */
        uint32_t latest_ts = 0;
        (void)edge_engine_get_latest(idx, &latest_val, &latest_ts);

        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "sensor", s_names[i]);
        cJSON_AddStringToObject(t, "unit",   s_units[i]);
        cJSON_AddNumberToObject(t, "current",         round_sensor_val(idx, latest_val));
        cJSON_AddNumberToObject(t, "pred_1h",         round_sensor_val(idx, an.regr[i].pred_1h));
        cJSON_AddNumberToObject(t, "slope_per_point", an.regr[i].slope);
        cJSON_AddNumberToObject(t, "timestamp",       (double)latest_ts);
        /* 趋势方向: 正=上升, 负=下降, ≈0=平稳 */
        const char *dir = "平稳";
        float slope = an.regr[i].slope;
        /* scale-relative threshold: 0.5% of phys range */
        float range = g_sensor_phys[i].phys_max - g_sensor_phys[i].phys_min;
        float thresh = range * 0.005f;
        if (slope >  thresh) dir = "上升";
        if (slope < -thresh) dir = "下降";
        cJSON_AddStringToObject(t, "trend_direction", dir);

        /* 附加当日汇总 (四舍五入到传感器精度) */
        if (an.daily[i].valid) {
            cJSON_AddNumberToObject(t, "today_min",    round_sensor_val(idx, an.daily[i].min));
            cJSON_AddNumberToObject(t, "today_max",    round_sensor_val(idx, an.daily[i].max));
            cJSON_AddNumberToObject(t, "today_mean",   round_sensor_val(idx, an.daily[i].mean));
            cJSON_AddNumberToObject(t, "reject_count", an.daily[i].reject);
        }

        cJSON_AddItemToArray(trends, t);
    }
    cJSON_AddItemToObject(o, "trends", trends);

    /* ── 传感器相关性 (最强的 5 对, |r|>0.3) ── */
    cJSON *corrs = cJSON_CreateArray();
    for (int i = 0; i < an.corr_n && i < 10; i++) {
        if (an.corr[i].r < 0.3f && an.corr[i].r > -0.3f) continue;
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "sensor_a", s_names[an.corr[i].a]);
        cJSON_AddStringToObject(c, "sensor_b", s_names[an.corr[i].b]);
        cJSON_AddNumberToObject(c, "pearson_r", an.corr[i].r);
        const char *rel = "正相关";
        if (an.corr[i].r < -0.3f) rel = "负相关";
        if (an.corr[i].r > -0.3f && an.corr[i].r < 0.3f) rel = "弱相关";
        cJSON_AddStringToObject(c, "relation", rel);
        cJSON_AddItemToArray(corrs, c);
    }
    cJSON_AddItemToObject(o, "correlations", corrs);

    /* 附加参考区间 (同 read_water_quality) */
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "temperature", "22~30°C (温水鱼)");
    cJSON_AddStringToObject(ref, "do",          ">5 mg/L 优良, <3 mg/L 危险");
    cJSON_AddStringToObject(ref, "ph",          "6.5~8.5 安全, 7.0~8.0 理想");
    cJSON_AddStringToObject(ref, "nh3n",        "<0.5 mg/L 安全, >1.0 mg/L 有毒");
    cJSON_AddItemToObject(o, "reference_ranges", ref);

    /* 分析提示 — 模型用这些信息做养殖方案推荐 */
    cJSON_AddStringToObject(o, "analysis_hint",
        "根据以上趋势和预测, 结合水质参考区间, 分析当前环境是否存在风险, "
        "并给出增氧时长、换水量、投喂量等养殖建议。");

    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* ============================================================
 * 工具注册表 (新增硬件只改这一处)
 * ============================================================ */

/* 读取闭环控制系统当前配置 (PID 参数 / 水泵阈值 / 投喂定时 / 模式 / 实时水质),
 * 供 AI 评估参数合理性并给出优化建议。只读 — 手动控制权交给人, AI 不直接修改。 */
static char* tool_read_control_config(const cJSON *args)
{
    (void)args;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);

    /* 增氧机 PID 参数 */
    float Kp = 0, Ki = 0, Kd = 0, sp = 0;
    app_action_control_get_pid(&Kp, &Ki, &Kd, &sp);
    cJSON *pid = cJSON_CreateObject();
    cJSON_AddNumberToObject(pid, "Kp", (double)Kp);
    cJSON_AddNumberToObject(pid, "Ki", (double)Ki);
    cJSON_AddNumberToObject(pid, "Kd", (double)Kd);
    cJSON_AddNumberToObject(pid, "setpoint_mgpl", (double)sp);
    cJSON_AddStringToObject(pid, "note", "增氧机 DO 闭环: 误差 e=SP-实测DO, 输出=PWM% (0~100)");
    cJSON_AddItemToObject(o, "aerator_pid", pid);

    /* 循环水泵阈值 */
    pump_threshold_t pt;
    app_action_control_get_pump_threshold(&pt);
    cJSON *pump = cJSON_CreateObject();
    cJSON_AddNumberToObject(pump, "temp_max_c", (double)pt.temp_max);
    cJSON_AddNumberToObject(pump, "ph_min",     (double)pt.ph_min);
    cJSON_AddNumberToObject(pump, "ph_max",     (double)pt.ph_max);
    cJSON_AddStringToObject(pump, "note", "超任一阈值 → 循环水泵自动开启, 全部恢复 → 停止");
    cJSON_AddItemToObject(o, "pump_threshold", pump);

    /* 投喂电机定时 */
    feeder_schedule_t fs;
    app_action_control_get_feeder_schedule(&fs);
    cJSON *feed = cJSON_CreateObject();
    cJSON_AddNumberToObject(feed, "feed_hour1",       fs.feed_hour1);
    cJSON_AddNumberToObject(feed, "feed_hour2",       fs.feed_hour2);
    cJSON_AddNumberToObject(feed, "duration_minutes", fs.feed_minutes);
    cJSON_AddStringToObject(feed, "note", "到点自动投喂, 持续设定时长后停止 (无反馈传感器, 纯定时)");
    cJSON_AddItemToObject(o, "feeder_schedule", feed);

    /* 当前模式 + 设备状态 + 实时水质 */
    control_status_t st;
    app_action_control_get_status(&st);
    cJSON *stat = cJSON_CreateObject();
    cJSON_AddStringToObject(stat, "mode", st.mode == CTRL_MODE_AUTO ? "auto" : "manual");
    cJSON_AddNumberToObject(stat, "aerator_pwm_pct", (double)st.aerator_duty);
    cJSON_AddBoolToObject  (stat, "pump_on",   st.pump_on);
    cJSON_AddBoolToObject  (stat, "feeder_on", st.feeder_on);
    cJSON_AddNumberToObject(stat, "current_do_mgpl", round_sensor_val(SENSOR_IDX_DO, st.do_val));
    cJSON_AddNumberToObject(stat, "current_temp_c",  round_sensor_val(SENSOR_IDX_TEMP, st.temp));
    cJSON_AddNumberToObject(stat, "current_ph",      round_sensor_val(SENSOR_IDX_PH, st.ph));
    cJSON_AddItemToObject(o, "current_status", stat);

    /* 建议生成指引 — 强调只读 + 手动权 */
    cJSON *adv = cJSON_CreateObject();
    cJSON_AddStringToObject(adv, "permission", "只读: 你只能读取配置并给出建议, 不能直接修改 (手动控制权交给人, 涉及安全)");
    cJSON_AddStringToObject(adv, "how_to_advise",
        "结合 current_status 实时水质与 reference 优化区间, 判断当前 PID/阈值/定时是否合理, "
        "给出建议值及理由。例: DO 长期低于 SP → 建议增大 Kp; 水温常超 32°C → 保持或调低 temp_max; "
        "投喂时段可结合鱼类摄食规律调整。结尾明确告知: 建议需用户在控制页 slider/弹窗人工确认后才生效。");
    cJSON_AddItemToObject(o, "advisory", adv);

    /* 优化参考区间 */
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "do",  "目标 5~6 mg/L, SP 建议 6.0 mg/L");
    cJSON_AddStringToObject(ref, "temp", "安全 <32°C");
    cJSON_AddStringToObject(ref, "ph",  "安全 6.5~8.5, 理想 7.0~8.0");
    cJSON_AddStringToObject(ref, "pid_tuning", "Kp 1~5, Ki 0.05~0.5, Kd 0.2~1.0 (DO 闭环典型初值)");
    cJSON_AddItemToObject(o, "reference", ref);

    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

static const ai_tool_t g_tools[] = {
    {
        "control_led",
        "控制 GEC6818 板载 LED (D7~D10) 的开关。led_id 1~4 对应 D7~D10。",
        "{\"type\":\"object\",\"properties\":{"
        "\"led_id\":{\"type\":\"integer\",\"description\":\"LED 编号 1-4 (1=D7, 2=D8, 3=D9, 4=D10)\"},"
        "\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"],\"description\":\"开或关\"}"
        "},\"required\":[\"led_id\",\"state\"]}",
        tool_control_led
    },
    {
        "control_all_leds",
        "一次性打开或关闭全部 4 路 LED。",
        "{\"type\":\"object\",\"properties\":{"
        "\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]}"
        "},\"required\":[\"state\"]}",
        tool_control_all_leds
    },
    {
        "random_led_on",
        "随机点亮一盏当前灭着的 LED; 若全部亮着则全部熄灭。用于演示。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_random_led_on
    },
    {
        "control_buzzer",
        "控制板载蜂鸣器 (GPIO78) 开或关。",
        "{\"type\":\"object\",\"properties\":{"
        "\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"]}"
        "},\"required\":[\"state\"]}",
        tool_control_buzzer
    },
    {
        "toggle_buzzer",
        "反转蜂鸣器状态 (响→静, 静→响)。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_toggle_buzzer
    },
    {
        "read_button",
        "读取板载 K2 按键 (GPIO28) 当前状态: 已按下 或 未按下。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_button
    },
    {
        "read_acceleration",
        "读取 MMA8653 三轴加速度计 (X/Y/Z 原始值 + g 值), 用于判断板子姿态/倾斜。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_acceleration
    },
    {
        "read_water_sensor",
        "读取智慧水产养殖 6 路水质传感器之一的最新值 (来自边缘引擎快照, 断电不丢)。",
        "{\"type\":\"object\",\"properties\":{"
        "\"sensor\":{\"type\":\"string\",\"enum\":"
        "[\"temperature\",\"humidity\",\"light\",\"do\",\"ph\",\"nh3n\"],"
        "\"description\":\"temperature=水温, humidity=湿度, light=光照, "
        "do=溶解氧, ph=pH值, nh3n=氨氮\"}"
        "},\"required\":[\"sensor\"]}",
        tool_read_water_sensor
    },
    {
        "read_water_quality",
        "一次性读取全部 6 路水质传感器 (温度/湿度/光照/溶氧/pH/氨氮) 的当前值 + "
        "当日汇总(min/max/mean/剔除数), 含水产养殖参考区间, 用于全面环境评价。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_water_quality
    },
    {
        "analyze_environment",
        "读取边缘引擎的深度分析结果: 每路传感器的 1h 预测值 + 趋势方向(上升/下降/平稳)"
        " + 传感器间 Pearson 相关性 + 当日汇总统计。用于智能推荐投喂量、增氧时长、"
        "换水策略等最优养殖方案, 以及预判设备故障、水质恶化趋势。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_analyze_environment
    },
    {
        "read_control_config",
        "读取闭环控制系统当前配置: 增氧机 PID 参数(Kp/Ki/Kd/SP), 循环水泵阈值(温度上限/pH上下限), "
        "投喂电机定时(两个时段+时长), 以及当前模式/设备状态/实时水质。用于评估控制参数是否合理并给出"
        "优化建议 (只读, 不修改 — 手动控制权交给人, 涉及安全)。",
        "{\"type\":\"object\",\"properties\":{}}",
        tool_read_control_config
    },
};
#define TOOL_COUNT (sizeof(g_tools) / sizeof(g_tools[0]))

/* ============================================================
 * 公开接口实现
 * ============================================================ */

const ai_tool_t* ai_tools_get_all(int *count)
{
    if (count) *count = (int)TOOL_COUNT;
    return g_tools;
}

char* ai_tools_build_schema_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < (int)TOOL_COUNT; i++) {
        cJSON *t  = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name",        g_tools[i].name);
        cJSON_AddStringToObject(fn, "description", g_tools[i].description);
        cJSON_AddItemToObject(fn, "parameters",
            cJSON_Parse(g_tools[i].parameters_json));  /* 静态字面量, 已校验 */
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }
    char *s = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return s;
}

char* ai_tools_dispatch(const char *name, const cJSON *args)
{
    for (int i = 0; i < (int)TOOL_COUNT; i++) {
        if (strcmp(name, g_tools[i].name) == 0) {
            return g_tools[i].handler(args);
        }
    }
    LV_LOG_USER("AI: unknown tool '%s'", name ? name : "(null)");
    return NULL;  /* 未注册的工具 */
}
