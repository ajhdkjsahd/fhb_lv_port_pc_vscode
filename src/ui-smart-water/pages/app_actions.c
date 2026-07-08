// ========== app_actions.c ==========
// 所有页面的平台相关回调函数实现。
//
//   分区 1：登录与注册    (login_page, register_page)
//   分区 2：视频播放器    (video_page)
//   分区 3：网络通讯      (network_page)
//   分区 4：AI 对话       (ai_chat_page)
//
#include "app_actions.h"
#include "video-page/video_page.h"
#include "../edge/edge_engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  全局配置与状态                                              ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

/* ── 视频： 文件夹路径与几何参数（需与 video_page.c 保持一致） ── */
#ifdef __linux__
#define VIDEOS_DIR      "/root/videos"
#define VIDEOS_DIR_FMT  "/root/videos/%s"
#else
#define VIDEOS_DIR      "A:../src/ui-smart-water/videos"
#define VIDEOS_DIR_FMT  "A:../src/ui-smart-water/videos/%s"
#endif

#define VID_X      16
#define VID_Y      65
#define VID_W      768
#define VID_H      305
#define MAX_VIDEOS 5

/* ── 登录： 保存的登录凭据 ── */
static char g_saved_user[33] = "";
static char g_saved_pass[33] = "";

/* ── 视频： 全局状态 ── */
static char ** g_video_paths     = NULL;
static int     g_video_count     = 0;
static int     g_video_index     = 0;
static char    g_cur_video_path[256] = "";
static lv_obj_t * g_video_screen = NULL;

#ifdef __linux__
static pid_t     g_mplayer_pid      = 0;
static int       g_video_pos        = 0;
static int       g_video_total_sec  = 0;
static bool      g_video_playing    = false;
static lv_timer_t * g_progress_timer = NULL;
static int       g_mp_fifo_fd       = -1;
#endif

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 1：登录与注册                                          ║
 *  ║  页面：login_page.c, register_page.c                         ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

/* ── 登录： 验证凭据是否与保存的一致 ── */
bool app_action_login_verify(const char * username, const char * password)
{
    LV_LOG_USER("Login attempt: user='%s'", username);

    if(g_saved_user[0] != '\0') {
        if(strcmp(username, g_saved_user) == 0 &&
           strcmp(password, g_saved_pass) == 0) {
            return true;
        }
    }

    if(strcmp(username, "a") == 0 &&
       strcmp(password, "a") == 0) {
        return true;
    }

    return false;
}

/* ── 注册： 提交新账号 ── */
bool app_action_register_submit(const char * username, const char * password)
{
    LV_LOG_USER("Register attempt: user='%s'", username);
    snprintf(g_saved_user, sizeof(g_saved_user), "%s", username);
    snprintf(g_saved_pass, sizeof(g_saved_pass), "%s", password);
    return true;
}

/* ── 登录： 通知登录成功 ── */
void app_action_login_success(void)
{
    LV_LOG_USER("Login success!");
}

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 2：视频播放器                                          ║
 *  ║  页面：video_page.c                                          ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/

/* ── 视频： 设置活跃屏幕指针（由 ui.c 调用） ── */
void app_action_set_video_screen(lv_obj_t * screen)
{
    g_video_screen = screen;
}

/* ── 视频： 扫描 videos/ 文件夹中的 .mp4 文件 ── */
void app_action_video_scan(void)
{
    lv_fs_dir_t dir;
    const char * dir_path = VIDEOS_DIR;
    char fn[256];
    int i;

    if(lv_fs_dir_open(&dir, dir_path) != LV_FS_RES_OK) {
        LV_LOG_USER("video: cannot open videos dir: %s", dir_path);
        g_video_count = 0;
        g_video_paths = NULL;
        return;
    }

    /* 统计 .mp4 文件（最多 256 次迭代，上限 MAX_VIDEOS） */
    int count = 0;
    for(i = 0; i < 256; i++) {
        if(lv_fs_dir_read(&dir, fn, sizeof(fn)) != LV_FS_RES_OK) break;
        if(fn[0] == '\0') continue;
        size_t len = strlen(fn);
        if(len >= 4 && strcmp(fn + len - 4, ".mp4") == 0) {
            if(count >= MAX_VIDEOS) break;
            count++;
        }
    }
    lv_fs_dir_close(&dir);

    LV_LOG_USER("video: found %d .mp4 files", count);

    if(count == 0) {
        g_video_count = 0;
        g_video_paths = NULL;
        return;
    }

    g_video_paths = lv_malloc(sizeof(char *) * count);
    if(!g_video_paths) { g_video_count = 0; return; }
    memset(g_video_paths, 0, sizeof(char *) * count);

    /* 收集文件名 */
    if(lv_fs_dir_open(&dir, dir_path) != LV_FS_RES_OK) {
        lv_free(g_video_paths);
        g_video_paths = NULL;
        g_video_count = 0;
        return;
    }

    int idx = 0;
    for(i = 0; i < 256 && idx < count; i++) {
        if(lv_fs_dir_read(&dir, fn, sizeof(fn)) != LV_FS_RES_OK) break;
        if(fn[0] == '\0') continue;
        size_t len = strlen(fn);
        if(len >= 4 && strcmp(fn + len - 4, ".mp4") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), VIDEOS_DIR_FMT, fn);
            g_video_paths[idx] = lv_malloc(strlen(full_path) + 1);
            if(g_video_paths[idx]) {
                strcpy(g_video_paths[idx], full_path);
                LV_LOG_USER("video: [%d] %s", idx, fn);
            }
            idx++;
        }
    }
    lv_fs_dir_close(&dir);
    g_video_count = idx;

    /* 初始化当前路径 */
    if(g_video_count > 0) {
        snprintf(g_cur_video_path, sizeof(g_cur_video_path),
                 "%s", g_video_paths[0]);
    }
}

const char * const * app_action_video_get_paths(void)
{
    return (const char * const *)g_video_paths;
}

int app_action_video_get_count(void)
{
    return g_video_count;
}

void app_action_video_select(int index)
{
    if(index < 0 || index >= g_video_count) return;
    if(index == g_video_index && g_cur_video_path[0] != '\0') return; /* already selected */

    LV_LOG_USER("video_select: switching to index %d", index);

    /* 切换前必须先停止正在运行的 mplayer */
    app_action_video_stop();

    g_video_index = index;
    snprintf(g_cur_video_path, sizeof(g_cur_video_path), "%s", g_video_paths[index]);
#ifdef __linux__
    g_video_total_sec = 0;
#endif

    LV_LOG_USER("video_select: now playing %s", g_cur_video_path);

    /* 更新界面 — 重置进度条 */
    video_page_update_progress(g_video_screen, 0, "00:00", "??:??");
    video_page_set_play_state(g_video_screen, false);
    video_page_set_video_active(g_video_screen, false);
}

const char * app_action_video_get_cover(int index)
{
    if(index < 0 || index >= g_video_count) return NULL;
#ifdef __linux__
    char cover_path[256];
    snprintf(cover_path, sizeof(cover_path),
             "/tmp/video_cover_%d.png", index);

    /* 已缓存时长？ */
    if(access(cover_path, F_OK) == 0) {
        char cached[300];
        snprintf(cached, sizeof(cached), "A:%s", cover_path);
        char * dup = lv_malloc(strlen(cached) + 1);
        if(dup) { strcpy(dup, cached); return dup; }
        return NULL;
    }

    /* 尝试用 mplayer 提取：在 /tmp 下运行，截帧图直接放在那里 */
    LV_LOG_USER("video_cover: extracting cover %d via mplayer...", index);
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "cd /tmp && "
             "mplayer -vo png -frames 1 -ss 1 -nosound -really-quiet "
             "\"%s\" >/dev/null 2>&1 && "
             "mv -f 00000001.png video_cover_%d.png",
             g_video_paths[index], index);
    int ret = system(cmd);

    if(ret == 0 && access(cover_path, F_OK) == 0) {
        /* 验证文件可读且有内容 */
        struct stat st;
        if(stat(cover_path, &st) == 0 && st.st_size > 100) {
            LV_LOG_USER("video_cover: OK for index %d (%ld bytes)", index, (long)st.st_size);
            char cached[300];
            snprintf(cached, sizeof(cached), "A:%s", cover_path);
            char * dup = lv_malloc(strlen(cached) + 1);
            if(dup) { strcpy(dup, cached); return dup; }
        } else {
            LV_LOG_USER("video_cover: file too small or gone for index %d", index);
        }
    } else {
        LV_LOG_USER("video_cover: mplayer failed for index %d (ret=%d)", index, ret);
    }
#endif
    return NULL;
}

/* ── 视频： WiFi/网络可达性检测（周期性定时器） ── */

wifi_status_t app_action_check_wifi(void)
{
#ifdef __linux__
    /* Strategy:
     *  1. Ping 8.8.8.8 (Google DNS) → external network.     GREEN if OK.
     *  2. Ping default gateway                        → LAN only.   YELLOW if OK.
     *  3. Neither                                      → no network. RED.
     *
     *  Each ping blocks up to ~1.5 s (1 packet, 2 s deadline via -w).
     *  Call infrequently — every 10-15 s from a timer — to avoid
     *  starving the LVGL main loop.
     */

    /* --- 第一步：外网检测 (WAN) --- */
    {
        int ret = system("ping -c 1 -w 2 8.8.8.8 >/dev/null 2>&1");
        if (ret == 0) {
            LV_LOG_USER("wifi: WAN reachable → GREEN");
            return WIFI_STATUS_GREEN;
        }
    }

    /* --- 第二步：网关检测 (LAN) --- */
    {
        /* 尝试从路由表读取默认网关 */
        char gw[32] = {0};
        FILE *fp = popen(
            "ip route show default 2>/dev/null | awk '{print $3}'", "r");
        if (fp) {
            if (fgets(gw, sizeof(gw), fp)) {
                size_t len = strlen(gw);
                if (len > 0 && gw[len - 1] == '\n') gw[len - 1] = '\0';
            }
            pclose(fp);
        }

        /* 拿到了网关地址，ping 一下 */
        if (gw[0] != '\0') {
            char cmd[64];
            snprintf(cmd, sizeof(cmd),
                     "ping -c 1 -w 2 %s >/dev/null 2>&1", gw);
            if (system(cmd) == 0) {
                LV_LOG_USER("wifi: WAN dead but LAN (%s) reachable → YELLOW", gw);
                return WIFI_STATUS_YELLOW;
            }
        }

        /* 回退：尝试常见网关地址 */
        const char * fallbacks[] = {
            "192.168.1.1", "192.168.0.1", "192.168.1.254", "10.0.0.1"
        };
        for (int i = 0; i < 4; i++) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd),
                     "ping -c 1 -w 1 %s >/dev/null 2>&1", fallbacks[i]);
            if (system(cmd) == 0) {
                LV_LOG_USER("wifi: WAN dead but %s reachable → YELLOW",
                            fallbacks[i]);
                return WIFI_STATUS_YELLOW;
            }
        }
    }

    /* --- 第三步：无网络可达 --- */
    LV_LOG_USER("wifi: no network → RED");
    return WIFI_STATUS_RED;

#else
    /* PC/Windows 桩 — 始终返回 GREEN 供 UI 模拟 */
    (void)0;
    return WIFI_STATUS_GREEN;
#endif
}

/**********************
 *   VIDEO — Linux / GEC6818 mplayer fbdev sub-region (slave mode via FIFO)
 **********************/
#ifdef __linux__

#define MP_CMD_FIFO  "/tmp/mplayer_cmd"

static void format_time(int sec, char * buf, size_t buf_size);
static void progress_timer_cb(lv_timer_t * timer);
static void mplayer_died(void);

/* ── MPlayer： FIFO 辅助函数 ── */

static void mp_fifo_open(void)
{
    if(g_mp_fifo_fd >= 0) return;
    /* Ignore SIGPIPE — mplayer may exit before we close the FIFO,
     * and a write to a broken pipe would kill the LVGL process. */
    signal(SIGPIPE, SIG_IGN);
    g_mp_fifo_fd = open(MP_CMD_FIFO, O_WRONLY);
    if(g_mp_fifo_fd < 0)
        LV_LOG_USER("mp_fifo_open: cannot open fifo");
}

static void mp_fifo_close(void)
{
    if(g_mp_fifo_fd >= 0) {
        close(g_mp_fifo_fd);
        g_mp_fifo_fd = -1;
    }
}

/* Send one command line to the running mplayer slave FIFO.
 * Returns true if the command was sent successfully. */
static bool mp_cmd(const char * cmd)
{
    if(g_mp_fifo_fd < 0) {
        mp_fifo_open();
        if(g_mp_fifo_fd < 0) return false;
    }
    size_t len = strlen(cmd);
    ssize_t w = write(g_mp_fifo_fd, cmd, len);
    if(w != (ssize_t)len) {
        /* mplayer may have exited (EPIPE) — clean up */
        LV_LOG_USER("mp_cmd: write failed (mplayer exited?)");
        mp_fifo_close();
        mplayer_died();
        return false;
    }
    write(g_mp_fifo_fd, "\n", 1);
    return true;
}

/* 从 mplayer -identify 查询视频时长（首次调用后缓存） */
static int get_video_duration_sec(void)
{
    if(g_video_total_sec > 0) return g_video_total_sec;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "mplayer -identify -vo null -ao null "
        "-frames 0 \"%s\" 2>/dev/null | grep ID_LENGTH",
        g_cur_video_path);

    FILE * fp = popen(cmd, "r");
    if(fp == NULL) {
        LV_LOG_USER("get_duration: popen failed");
        return 0;
    }

    char buf[64] = {0};
    if(fgets(buf, sizeof(buf), fp)) {
        const char * eq = strchr(buf, '=');
        if(eq) {
            float sec = atof(eq + 1);
            g_video_total_sec = (int)(sec + 0.5f);
            if(g_video_total_sec < 1) g_video_total_sec = 1;
            LV_LOG_USER("video duration: %d sec", g_video_total_sec);
        }
    }
    pclose(fp);
    return g_video_total_sec;
}

/* ── MPlayer： 强制杀死所有 mplayer 进程（含僵尸进程） ── */
static void mplayer_force_kill(void)
{
    /* 如果有 PID，按 PID 杀 */
    if(g_mplayer_pid != 0) {
        LV_LOG_USER("mplayer_force_kill: killing pid=%d", (int)g_mplayer_pid);
        kill(g_mplayer_pid, SIGKILL);
        usleep(100000);
        g_mplayer_pid = 0;
    }
    /* 同时杀死所有残留的 mplayer 进程 */
    system("killall -9 mplayer 2>/dev/null");
    usleep(50000);

    /* 清理 FIFO 和定时器 */
    mp_fifo_close();
    unlink(MP_CMD_FIFO);

    if(g_progress_timer) {
        lv_timer_delete(g_progress_timer);
        g_progress_timer = NULL;
    }

    g_video_playing = false;
    g_video_pos = 0;
}

/* ── MPlayer： 以 slave 模式启动（先杀旧进程） ── */
static void mplayer_start(int start_pos)
{
    /* 强制杀死已有的 mplayer — 绝不允许两个同时运行 */
    mplayer_force_kill();

    LV_LOG_USER("mplayer_start: path=%s pos=%d", g_cur_video_path, start_pos);

    bool first_duration = (g_video_total_sec == 0);
    int total = get_video_duration_sec();
    if(total < 1) total = 236;

    /* 创建命令 FIFO */
    unlink(MP_CMD_FIFO);
    if(mkfifo(MP_CMD_FIFO, 0666) < 0) {
        LV_LOG_USER("mplayer_start: cannot create fifo %s", MP_CMD_FIFO);
        return;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mplayer -vo fbdev -ao oss "
        "-slave -input file=%s "
        "-zoom -x %d -y %d "
        "-geometry %d:%d "
        "-ss %d "
        "-nodouble -really-quiet "
        "\"%s\" >/dev/null 2>&1 &",
        MP_CMD_FIFO,
        VID_W, VID_H,
        VID_X, VID_Y,
        start_pos,
        g_cur_video_path);
    int ret = system(cmd);

    LV_LOG_USER("mplayer_start: system ret=%d", ret);

    if(ret == 0) {
        /* 等待 mplayer 启动 */
        usleep(200000);

        FILE * fp = popen("pidof mplayer", "r");
        if(fp) {
            char buf[32] = {0};
            if(fgets(buf, sizeof(buf), fp)) {
                g_mplayer_pid = (pid_t)atoi(buf);
                g_video_pos   = start_pos;
                g_video_playing = true;
                LV_LOG_USER("mplayer_start: pid=%d", (int)g_mplayer_pid);

                if(first_duration) {
                    video_page_set_duration(g_video_screen, total);
                }
                char current[16];
                char total_str[16];
                format_time(g_video_pos, current, sizeof(current));
                format_time(g_video_total_sec, total_str, sizeof(total_str));
                video_page_update_progress(g_video_screen, g_video_pos, current, total_str);
                video_page_set_video_active(g_video_screen, true);
                video_page_set_play_state(g_video_screen, true);

                if(g_progress_timer == NULL) {
                    g_progress_timer = lv_timer_create(progress_timer_cb, 1000, NULL);
                }
                mp_fifo_open();
            } else {
                LV_LOG_USER("mplayer_start: pidof returned empty, mplayer may have failed");
            }
            pclose(fp);
        }
    } else {
        LV_LOG_USER("mplayer_start: system() failed, ret=%d", ret);
        unlink(MP_CMD_FIFO);
    }
}

/* mplayer 异常退出时的回调（EOF、崩溃等） */
static void mplayer_died(void)
{
    LV_LOG_USER("mplayer died, cleaning up");
    g_mplayer_pid = 0;
    g_video_playing = false;

    if(g_progress_timer) {
        lv_timer_delete(g_progress_timer);
        g_progress_timer = NULL;
    }

    mp_fifo_close();
    unlink(MP_CMD_FIFO);
    video_page_set_play_state(g_video_screen, false);
    video_page_set_video_active(g_video_screen, false);
    /* 重置进度，让用户可以重播 */
    video_page_update_progress(g_video_screen, 0, "00:00", "??:??");
}

static void mplayer_stop(void)
{
    if(g_mplayer_pid == 0) return;  /* mplayer 没启动, 无需停止 (防止从其他页面回首页时"串台") */
    LV_LOG_USER("mplayer_stop: force-killing mplayer");
    mplayer_force_kill();
    video_page_set_play_state(g_video_screen, false);
    video_page_set_video_active(g_video_screen, false);
    video_page_update_progress(g_video_screen, 0, "00:00", "??:??");
}

/* ── MPlayer： 通过 FIFO 发送 slave 命令（不杀进程、不重启） ── */

static void mplayer_pause(void)
{
    if(g_mplayer_pid == 0 || !g_video_playing) return;
    LV_LOG_USER("Pausing mplayer (slave pause)");
    mp_cmd("pause");
    g_video_playing = false;
    video_page_set_play_state(g_video_screen, false);
}

static void mplayer_resume(void)
{
    if(g_mplayer_pid == 0 || g_video_playing) return;
    LV_LOG_USER("Resuming mplayer (slave pause)");
    mp_cmd("pause");
    g_video_playing = true;
    video_page_set_play_state(g_video_screen, true);
}

static void mplayer_seek_rel(int delta_sec)
{
    if(g_mplayer_pid == 0) return;
    g_video_pos += delta_sec;
    if(g_video_pos < 0) g_video_pos = 0;
    if(g_video_pos > g_video_total_sec) g_video_pos = g_video_total_sec;

    LV_LOG_USER("Seeking relative %+d → %d sec", delta_sec, g_video_pos);
    char c[32];
    snprintf(c, sizeof(c), "seek %d 0", delta_sec);
    mp_cmd(c);
}

static void mplayer_seek_abs(int sec)
{
    if(g_mplayer_pid == 0) return;
    g_video_pos = sec;
    if(g_video_pos < 0) g_video_pos = 0;
    if(g_video_pos > g_video_total_sec) g_video_pos = g_video_total_sec;

    LV_LOG_USER("Seeking to %d sec", g_video_pos);
    char c[32];
    snprintf(c, sizeof(c), "seek %d 2", g_video_pos);
    mp_cmd(c);
}

/* ── 视频： 公开 API（由 video_page.c 调用） ── */

void app_action_video_stop(void)
{
    mplayer_stop();
}

void app_action_video_control(video_action_t action)
{
    switch(action) {
        case VIDEO_ACTION_PLAY_PAUSE:
            if(g_mplayer_pid == 0) {
                mplayer_start(g_video_pos);
            } else if(g_video_playing) {
                mplayer_pause();
            } else {
                mplayer_resume();
            }
            break;

        case VIDEO_ACTION_REWIND:
            mplayer_seek_rel(-5);
            break;

        case VIDEO_ACTION_FAST_FORWARD:
            mplayer_seek_rel(+5);
            break;

        case VIDEO_ACTION_VOLUME_UP:
            system("amixer set PCM 5%+ 2>/dev/null");
            break;

        case VIDEO_ACTION_VOLUME_DOWN:
            system("amixer set PCM 5%- 2>/dev/null");
            break;
    }
}

void app_action_video_seek(int32_t position)
{
    mplayer_seek_abs(position);

    char current[16];
    char total[16];
    format_time(g_video_pos, current, sizeof(current));
    format_time(g_video_total_sec, total, sizeof(total));
    video_page_update_progress(g_video_screen, g_video_pos, current, total);
}

/* ── MPlayer： 进度定时器与时间格式化 ── */

static void progress_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(!g_video_playing) return;

    /* 检测 mplayer 是否退出（EOF）— kill(pid, 0) 返回 -1 表示进程已消失 */
    if(g_mplayer_pid != 0 && kill(g_mplayer_pid, 0) != 0) {
        mplayer_died();
        return;
    }

    if(g_video_pos < g_video_total_sec) {
        g_video_pos++;
    }

    /* 每两拍刷新一次 UI，让滑块丝滑 */
    static int tick = 0;
    if(++tick >= 1) {
        tick = 0;
        char current[16];
        char total[16];
        format_time(g_video_pos, current, sizeof(current));
        format_time(g_video_total_sec, total, sizeof(total));
        video_page_update_progress(g_video_screen, g_video_pos, current, total);
    }
}

static void format_time(int sec, char * buf, size_t buf_size)
{
    if(sec < 0) sec = 0;
    snprintf(buf, buf_size, "%02d:%02d", sec / 60, sec % 60);
}

#else  /* !__linux__ — PC/Windows 桩 */

void app_action_video_stop(void)
{
    LV_LOG_USER("Video: stop (stub)");
}

void app_action_video_control(video_action_t action)
{
    switch(action) {
        case VIDEO_ACTION_PLAY_PAUSE:
            LV_LOG_USER("Video: toggle play/pause (stub)");
            break;
        case VIDEO_ACTION_REWIND:
            LV_LOG_USER("Video: rewind 5s (stub)");
            break;
        case VIDEO_ACTION_FAST_FORWARD:
            LV_LOG_USER("Video: fast forward 5s (stub)");
            break;
        case VIDEO_ACTION_VOLUME_UP:
            LV_LOG_USER("Video: volume up (stub)");
            break;
        case VIDEO_ACTION_VOLUME_DOWN:
            LV_LOG_USER("Video: volume down (stub)");
            break;
    }
}

void app_action_video_seek(int32_t position)
{
    LV_LOG_USER("Video: seek to %ld/1000 (stub)", (long)position);
}

#endif /* __linux__ 视频 mplayer 控制块 */

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 3：网络通讯                                            ║
 *  ║  页面：network_page.c                                        ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *  socket → connect → spawn recv_thread → LVGL-safe UI updates
 ***********************************************************************/
#include "network-page/network_page.h"

static lv_obj_t * g_network_screen = NULL;

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define TCP_BUF_SIZE  1024

static int       g_sock       = -1;   /* TCP socket fd */
static pthread_t g_recv_tid;         /* 接收线程 */
static bool      g_recv_run  = false; /* 线程运行标志 */

/* ── 网络： 线程安全的 UI 更新（lv_async_call） ── */
static void tcp_update_screen(void * data)
{
    if (g_network_screen == NULL || data == NULL) return;
    char * msg = (char *)data;
    network_page_append_message(g_network_screen, NETWORK_MSG_RECV, msg);
    lv_free(msg);
}

/* ── 网络： TCP 接收线程 ── */
static void * tcp_recv_thread(void * arg)
{
    (void)arg;
    char buf[TCP_BUF_SIZE];

    while (g_recv_run) {
        int n = read(g_sock, buf, sizeof(buf) - 1);
        if (n <= 0) {
            /* 连接关闭或出错 */
            lv_async_call(tcp_update_screen, strdup("服务器已断开"));
            break;
        }
        buf[n] = '\0';
        /* 去除末尾 \n */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (len > 1 && buf[len - 2] == '\r') buf[len - 2] = '\0';

        /* 拷贝到堆 — lv_async_call 会负责释放 */
        lv_async_call(tcp_update_screen, strdup(buf));
    }

    /* 线程退出时清理 */
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    g_recv_run = false;
    /* 更新界面 */
    lv_async_call(tcp_update_screen, strdup("[client] 连接已断开"));
    return NULL;
}

/* ── 网络： TCP 连接 ── */
static bool tcp_connect(const char * ip, int port)
{
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        LV_LOG_USER("tcp: socket() failed: %s", strerror(errno));
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LV_LOG_USER("tcp: connect() failed: %s", strerror(errno));
        close(g_sock);
        g_sock = -1;
        return false;
    }

    LV_LOG_USER("tcp: connected to %s:%d (fd=%d)", ip, port, g_sock);

    /* Spawn receive thread */
    g_recv_run = true;
    if (pthread_create(&g_recv_tid, NULL, tcp_recv_thread, NULL) != 0) {
        LV_LOG_USER("tcp: pthread_create failed");
        close(g_sock);
        g_sock = -1;
        g_recv_run = false;
        return false;
    }
    pthread_detach(g_recv_tid);

    return true;
}

/* ── 网络： TCP 断开连接 ── */
static void tcp_disconnect(void)
{
    g_recv_run = false;
    if (g_sock >= 0) {
        shutdown(g_sock, SHUT_RDWR);  /* wake up recv thread from read() */
        close(g_sock);
        g_sock = -1;
    }
    /* Give recv thread a moment to exit */
    usleep(100000);
}

#endif /* __linux__ 视频 mplayer 控制块 */

/* ── 网络： 公开 API（由 network_page.c 调用） ── */

void app_action_set_network_screen(lv_obj_t * screen)
{
    g_network_screen = screen;
}

void app_action_network_connect(const char * ip, const char * port)
{
    LV_LOG_USER("Network: connect to %s:%s", ip, port);

#ifdef __linux__
    tcp_disconnect();

    network_page_append_message(g_network_screen, NETWORK_MSG_INFO,
                                "正在建立 TCP 连接…");

    int p = atoi(port);
    if (p <= 0 || p > 65535) {
        network_page_append_message(g_network_screen, NETWORK_MSG_ERROR,
                                    "端口号无效");
        return;
    }

    if (tcp_connect(ip, p)) {
        char msg[128];
        network_page_set_connected(g_network_screen, true);
        network_page_append_message(g_network_screen, NETWORK_MSG_INFO,
                                    "TCP 连接成功");
        snprintf(msg, sizeof(msg), "已连接到 %s:%s", ip, port);
        network_page_append_message(g_network_screen, NETWORK_MSG_RECV, msg);
    } else {
        network_page_append_message(g_network_screen, NETWORK_MSG_ERROR,
                                    "TCP 连接失败");
    }
#else
    /* PC stub */
    network_page_append_message(g_network_screen, NETWORK_MSG_INFO,
                                "正在建立 TCP 连接…");
    network_page_set_connected(g_network_screen, true);
    network_page_append_message(g_network_screen, NETWORK_MSG_INFO,
                                "已成功连接到服务器 (PC模拟)");
    network_page_append_message(g_network_screen, NETWORK_MSG_RECV,
                                "服务器: 欢迎连接智慧水产系统");
#endif
}

void app_action_network_disconnect(void)
{
    LV_LOG_USER("Network: disconnect");

#ifdef __linux__
    tcp_disconnect();
#endif
    network_page_set_connected(g_network_screen, false);
    network_page_append_message(g_network_screen, NETWORK_MSG_INFO,
                                "已断开连接");
}

void app_action_network_send(const char * message)
{
    LV_LOG_USER("Network: send '%s'", message);

    network_page_append_message(g_network_screen, NETWORK_MSG_SEND, message);

#ifdef __linux__
    if (g_sock >= 0) {
        ssize_t w = write(g_sock, message, strlen(message));
        if (w < 0) {
            network_page_append_message(g_network_screen, NETWORK_MSG_ERROR,
                                        "发送失败");
        }
        write(g_sock, "\n", 1);  /* newline terminator */
    } else {
        network_page_append_message(g_network_screen, NETWORK_MSG_ERROR,
                                    "未连接到服务器");
    }
#else
    /* PC stub */
    network_page_append_message(g_network_screen, NETWORK_MSG_RECV,
                                "已收到数据 (PC模拟)");
#endif
}

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 4：AI 对话 — Qwen2.5:7b 原生 Function Calling          ║
 *  ║  页面：ai_chat_page.c (UI 不变)                              ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *  重逻辑下沉到 ai_agent.c (HTTP + tool_calls 闭环) + ai_tools.c
 *  (工具注册表 → ai_hardware 真硬件 + edge_engine 真传感器)。
 *  本分区只做薄壳: init/send/stop 转调 agent, 外加一个事件→UI
 *  适配回调 (单次 lv_async_call 包, 保证 thinking/action/answer 顺序)。
 ***********************************************************************/
#include "ai-chat-page/ai_chat_page.h"
#include "ai-chat-page/ai_agent.h"

/* ── AI： 系统提示词 (模型人设 + 能力说明 + 水产知识; 工具 schema 由 agent 自动注入) ── */
#define AI_SYSTEM_PROMPT \
    "你是智慧水产养殖AI助手，运行在 GEC6818 ARM Linux 嵌入式板卡上。" \
    "你具备四大核心能力:" \
    " 1. 自动解读水质环境状态并给出评价 — 用 read_water_quality 一次性获取全部 6 路传感器" \
    "    (温度/湿度/光照/溶解氧/pH值/氨氮), 对照参考区间判断水质是否健康。" \
    " 2. 智能解答水产养殖和设备运维问题 — 结合养殖知识与板载硬件状态回答用户。" \
    " 3. 结合运行日志定位故障 — 用 analyze_environment 查看趋势/预测/相关性/异常剔除数," \
    "    若某路传感器 reject_count 异常偏高或趋势突变, 提示可能传感器故障或设备异常。" \
    " 4. 智能推荐投喂量/增氧时长等最优方案 — 用 analyze_environment 获取 1h 预测 + 趋势," \
    "    结合 DO/pH/水温预测值推荐增氧时长; 参考水温+溶氧推荐投喂量。" \
    "" \
    "★ 关键行为准则 (必须遵守):" \
    "  A. 任何时候用户询问水质相关话题 (溶氧/pH/氨氮/水温/能否养殖/是不是生病/投喂/增氧)," \
    "     必须第一时间调用 read_water_quality 获取全部 6 路实时传感器数据;" \
    "     永远不要凭空猜测当前水质, 必须基于工具返回的真实数据再给出分析和建议。" \
    "  B. 当用户提出养殖方案建议 (投喂量/增氧时长/换水策略), 先用 read_water_quality" \
    "     获取当前值, 再用 analyze_environment 获取趋势和预测, 然后综合给出方案。" \
    "  C. 每次用户要求操作硬件 (LED/蜂鸣器) 都必须调用对应工具;" \
    "     即使看起来是重复操作也要执行, 不要假设设备已经处于某种状态。" \
    "  D. 先调用工具获得数据 → 再分析和回复。绝对不要跳过工具调用直接编造数据。" \
    "" \
    "水质参考区间 (温水鱼 e.g. 罗非鱼/草鱼/鲤鱼):" \
    "  水温 22~30°C 理想, <20°C 摄食减少, >32°C 需增氧" \
    "  溶解氧 >5 mg/L 优良, 3~5 偏低需增氧, <3 危险立即开增氧机" \
    "  pH 6.5~8.5 安全, 7.0~8.0 理想" \
    "  氨氮 <0.5 mg/L 安全, 0.5~1.0 偏高需换水, >1.0 有毒立即处理" \
    "  光照 40~80% 日间正常" \
    "" \
    "投喂建议参考 (日投喂率=饲料重/鱼体重):" \
    "  水温 22~25°C: 2~3% 体重/日, 分 2~3 次" \
    "  水温 25~30°C: 3~4% 体重/日, 分 3~4 次" \
    "  水温 <20°C: 1~2%, 减少投喂" \
    "  溶氧 <3 mg/L 或 pH 异常: 暂停投喂, 先增氧/换水" \
    "" \
    "增氧时长建议:" \
    "  溶氧 >5: 正常无需额外增氧" \
    "  溶氧 4~5: 开增氧机 2~4 小时" \
    "  溶氧 3~4: 开增氧机 4~6 小时, 监测回升" \
    "  溶氧 <3: 立即全开增氧机 + 减少投喂, 直到回升到 >5" \
    "" \
    "板载硬件 (通过工具控制, 用于演示/调试):" \
    "  LED D7~D10 (led1~4): control_led 开关, random_led_on 随机点亮" \
    "  蜂鸣器 (GPIO78): control_buzzer / toggle_buzzer" \
    "  K2 按键 (GPIO28): read_button, 按下=0" \
    "  MMA8653 加速度计: read_acceleration 检测板子姿态/振动" \
    "" \
    "回复要求: 始终用简体中文。先分析数据, 再给出结论和建议。数据异常时优先判断" \
    "是传感器故障还是真实环境变化(连续多点和趋势能区分)。回复简洁但覆盖关键点。"

/* ── AI： 屏幕指针 ── */
static lv_obj_t * g_ai_screen = NULL;

/* ── AI： UI 数据包 (一次请求的 thinking/action/answer/error 打包) ── */
typedef struct {
    char *thinking;   /* malloc'd, 累计思考 + 工具调用行, 可为 NULL */
    char *answer;     /* malloc'd, 可为 NULL */
    char *error;      /* malloc'd, 可为 NULL */
} ai_ui_packet_t;

/* 在主线程执行 (由 lv_async_call 调度) — 单次调用保证顺序 */
static void ai_ui_show(void * data)
{
    ai_ui_packet_t *p = (ai_ui_packet_t *)data;
    LV_LOG_USER("AI: ui_show screen=%p err=%p think=%p ans=%p",
                (void*)g_ai_screen, (void*)p->error,
                (void*)p->thinking, (void*)p->answer);

    if(g_ai_screen == NULL) goto cleanup;

    if(p->error) {
        ai_chat_page_show_error(g_ai_screen, p->error);
    } else {
        /* 全部步骤在一次回调内完成 — 顺序绝对安全 */
        ai_chat_page_begin_response(g_ai_screen);
        if(p->thinking && p->thinking[0]) {
            ai_chat_page_append_thinking(g_ai_screen, p->thinking);
        }
        ai_chat_page_finish_thinking(g_ai_screen);
        if(p->answer && p->answer[0]) {
            ai_chat_page_append_answer(g_ai_screen, p->answer);
        }
        ai_chat_page_finish_response(g_ai_screen);
    }

cleanup:
    free(p->thinking);
    free(p->answer);
    free(p->error);
    lv_free(p);
}

/* ── AI： 字符串追加 (换行分隔, LVGL 堆) ── */
static char * str_append_line(char *dst, const char *src)
{
    if(!src || !src[0]) return dst;
    size_t dl = dst ? strlen(dst) : 0;
    size_t sl = strlen(src);
    char *t = lv_malloc(dl + sl + 2);   /* +1 换行 +1 NUL */
    if(!t) return dst;
    if(dl) { memcpy(t, dst, dl); t[dl++] = '\n'; }
    memcpy(t + dl, src, sl);
    t[dl + sl] = '\0';
    lv_free(dst);
    return t;
}

/* ── AI： 适配回调 (worker 线程触发, 累计事件, DONE 时一次性投递) ──
 *  per-request 包: agent 保证同一时刻只有一个 worker, 故可用静态指针 */
static ai_ui_packet_t * g_pkt = NULL;

static void on_event(const ai_event_t * evt, void * user_data)
{
    (void)user_data;
    if(!g_pkt) {
        g_pkt = lv_malloc_zeroed(sizeof(*g_pkt));
        if(!g_pkt) return;
    }

    switch(evt->type) {
    case AI_EVT_THINKING:
        g_pkt->thinking = str_append_line(g_pkt->thinking, evt->text);
        break;
    case AI_EVT_ACTION: {
        /* 工具调用作为一行塞进思考面板, 用户能看到 "执行了什么" */
        char line[320];
        snprintf(line, sizeof(line), "[执行 %s] %s",
                 evt->tool ? evt->tool : "?",
                 evt->result ? evt->result : "(无结果)");
        g_pkt->thinking = str_append_line(g_pkt->thinking, line);
        break;
    }
    case AI_EVT_ANSWER:
        lv_free(g_pkt->answer);
        g_pkt->answer = evt->text ? strdup(evt->text) : NULL;
        break;
    case AI_EVT_ERROR:
        lv_free(g_pkt->error);
        g_pkt->error = evt->text ? strdup(evt->text) : strdup("未知错误");
        break;
    case AI_EVT_DONE:
        /* 单次 lv_async_call 投递整个包 — 保证 UI 更新顺序 */
        lv_async_call(ai_ui_show, g_pkt);
        g_pkt = NULL;
        break;
    }
}

/* ── AI： 公开 API (由 ai_chat_page.c 和 ui.c 调用) ── */

void app_action_ai_init(void)
{
    /* host/port/model 传 NULL/0 → 沿用 ai_agent.c 编译期默认
     *   板子 (__linux__): 192.168.137.1:11434
     *   PC            : localhost:11434 (仅模拟分支, 不真连) */
    ai_agent_init(NULL, 0, NULL);
    ai_agent_set_system(AI_SYSTEM_PROMPT);
    ai_agent_set_callback(on_event, NULL);
    LV_LOG_USER("AI: Ollama FC client ready (qwen2.5:7b)");
}

void app_action_ai_set_screen(lv_obj_t * screen)
{
    g_ai_screen = screen;
}

void app_action_ai_send(const char * message)
{
    if(message == NULL || message[0] == '\0') return;
    if(!ai_agent_send(message)) {
        LV_LOG_USER("AI: already running, ignoring send");
    }
}

void app_action_ai_stop(void)
{
    ai_agent_stop();
}

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 5：传感器数据 — 边缘引擎快照                            ║
 *  ║  页面：sensor_page.c / trend_page.c                           ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *  数据流: app_mqtt.c 收到 JSON → edge_engine_push(整帧) → worker 线程
 *          去重/异常过滤/写 eMMC CSV/更新快照+历史环/周期分析。
 *  sensor_page 定时器 → app_action_sensor_read() = edge_engine_get_latest()。
 *  断网/重启后快照由引擎从 CSV 重建, 卡片不丢数据。
 *  量程与告警阈值表在 sensor_page.c 中维护（展示层）；物理量程在
 *  sensor_range.h（引擎过滤层）。
 ***********************************************************************/

/* ── 数据来源：边缘引擎快照（edge_engine 内部线程安全） ──
 *  MQTT/PC sim → edge_engine_push → worker 清洗+持久化+更新快照。
 *  本层不再持有任何传感器状态, 仅做转发, 断网/重启后快照由引擎从 eMMC 重建。 */

bool app_action_sensor_read(sensor_idx_t idx, float * value)
{
    return edge_engine_get_latest(idx, value, NULL);
}

/* 向后兼容入口：实际数据流由 app_mqtt.c 直接调 edge_engine_push(整帧)。
 *  单路写入无法构成完整帧, 此处仅作占位, 不再使用。 */
void app_action_sensor_set(sensor_idx_t idx, float value)
{
    (void)idx; (void)value;
}

/* 旧版「断连清空」: 引擎持久化后保留数据, 不再清空, 仅打日志。 */
void app_action_sensor_reset_all(void)
{
    printf("[sensor] MQTT disconnected — edge engine retains last known data\n");
}

/***********************************************************************
 *  ╔══════════════════════════════════════════════════════════════╗
 *  ║  分区 6：闭环控制 — PID + PWM  (control_page)                ║
 *  ║  薄 shim: 转发到 control/ 模块, UI 不直接碰控制逻辑。        ║
 *  ║  控制逻辑(PID算法/PWM sysfs/闭环引擎)全部在 control/ 下,     ║
 *  ║  与 LVGL 完全解耦; 本层只做参数透传与状态查询。              ║
 *  ╚══════════════════════════════════════════════════════════════╝
 ***********************************************************************/
#include "../control/control_loop.h"

void          app_action_control_init(void)             { control_init(); }
void          app_action_control_step(void)              { control_step(); }
void          app_action_control_set_mode(ctrl_mode_t m) { control_set_mode(m); }
ctrl_mode_t   app_action_control_get_mode(void)          { return control_get_mode(); }
void          app_action_control_estop(void)             { control_estop(); }
void          app_action_control_clear_estop(void)       { control_clear_estop(); }

void app_action_control_set_pid_gains(float Kp, float Ki, float Kd)
{ control_set_pid_gains(Kp, Ki, Kd); }

void app_action_control_set_aerator_sp(float sp)
{ control_set_aerator_sp(sp); }

void app_action_control_set_pump_threshold(float temp_max, float ph_min, float ph_max)
{ control_set_pump_threshold(temp_max, ph_min, ph_max); }

void app_action_control_set_feeder_schedule(int h1, int h2, int minutes)
{ control_set_feeder_schedule(h1, h2, minutes); }

void app_action_control_set_feeder_duty(float duty_pct)
{ control_set_feeder_duty(duty_pct); }

void app_action_control_manual_set_aerator(float duty_pct)
{ control_manual_set_aerator(duty_pct); }

void app_action_control_manual_set_pump(bool on)
{ control_manual_set_pump(on); }

void app_action_control_manual_feed_trigger(void)
{ control_manual_feed_trigger(); }

void app_action_control_get_status(control_status_t *out)
{ control_get_status(out); }

void app_action_control_get_pid(float *Kp, float *Ki, float *Kd, float *sp)
{ control_get_pid(Kp, Ki, Kd, sp); }

void app_action_control_get_pump_threshold(pump_threshold_t *out)
{ control_get_pump_threshold(out); }

void app_action_control_get_feeder_schedule(feeder_schedule_t *out)
{ control_get_feeder_schedule(out); }

int app_action_control_get_pv_history(float *buf, int max)
{ return control_get_pv_history(buf, max); }

int app_action_control_get_sp_history(float *buf, int max)
{ return control_get_sp_history(buf, max); }
