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
 *  ║  分区 4：AI 对话 — Ollama / DeepSeek-R1                      ║
 *  ║  页面：ai_chat_page.c                                        ║
 *  ╚══════════════════════════════════════════════════════════════╝
 *  HTTP POST → /api/chat (non-streaming) → parse JSON → split
 *  <think> into thinking panel → display answer in bubble.
 *  Pure POSIX sockets via http_client.h. Pthread recv → lv_async_call.
 ***********************************************************************/
#include "ai-chat-page/ai_chat_page.h"
#include "ai-chat-page/http_client.h"
#include "ai-chat-page/ai_hardware.h"
#include <pthread.h>

/* ── AI： Ollama 服务器配置 ── */
#ifdef __linux__
#define OLLAMA_HOST       "192.168.137.1"
#else
#define OLLAMA_HOST       "localhost"
#endif
#define OLLAMA_PORT       11434
#define OLLAMA_MODEL      "deepseek-r1:7b"
#define OLLAMA_TMO        300  /* 5 min — DeepSeek-R1 model loading can be slow */
#define OLLAMA_SYSTEM_MSG \
    "你是智慧水产养殖AI助手，精通水质管理、鱼病诊断、投喂策略、养殖技术。" \
    "硬件操作(开灯/蜂鸣器/传感器)由系统自动执行，" \
    "用户消息中「(系统已执行: ...)」表示操作已完成，" \
    "你只需根据结果自然回复，无需重复操作。"

/* ── AI： 状态变量 ── */
static lv_obj_t * g_ai_screen  = NULL;
static int        g_ai_sock    = -1;   /* socket to close on stop */
static pthread_t  g_ai_thread;
static bool       g_ai_running = false;
static bool       g_ai_stop    = false;

/* ── AI： 对话历史（环形缓冲区，最多 100 轮） ── */
#define AI_HIST_MAX 100
static char * g_ai_hist[AI_HIST_MAX];
static int    g_ai_hist_n    = 0;
static int    g_ai_hist_head = 0;

/* ── AI： JSON 辅助函数（零依赖，来自 gec6818_ollama_client） ── */

static char* ai_json_val(const char *s, const char *key)
{
    char pat[256];
    int pl = snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    if (pl < 1 || pl >= (int)sizeof(pat)) return NULL;
    const char *p = strstr(s, pat);
    if (!p) return NULL;
    p += pl;
    const char *end = p;
    while (*end && *end != '"') { if (*end == '\\' && end[1]) end++; end++; }
    if (!*end) return NULL;
    size_t len = (size_t)(end - p);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    char *d = out;
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n':  *d++ = '\n'; p++; continue;
                case 't':  *d++ = '\t'; p++; continue;
                case 'r':  *d++ = '\r'; p++; continue;
                case '"':  *d++ = '"';  p++; continue;
                case '\\': *d++ = '\\'; p++; continue;
                case 'u': {
                    /* 解码 \uXXXX（Ollama 会将 < > HTML 转义为 < >） */
                    p++;  /* skip 'u' */
                    if (p + 4 <= end) {
                        char hex[5];
                        memcpy(hex, p, 4); hex[4] = '\0';
                        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                        if (cp < 0x80) {
                            *d++ = (char)cp;
                        } else if (cp < 0x800) {
                            *d++ = (char)(0xC0 | (cp >> 6));
                            *d++ = (char)(0x80 | (cp & 0x3F));
                        } else {
                            *d++ = (char)(0xE0 | (cp >> 12));
                            *d++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                            *d++ = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    p += 4;
                    continue;
                }
            }
        }
        *d++ = *p++;
    }
    *d = '\0';
    return out;
}

static char* ai_json_esc(const char *src)
{
    if (!src) return strdup("");
    size_t cap = strlen(src) + 128;
    char *buf = malloc(cap), *d = buf;
    if (!buf) return NULL;
    while (*src) {
        if ((size_t)(d - buf) + 3 > cap) {
            size_t off = (size_t)(d - buf);
            cap *= 2;
            char *t = realloc(buf, cap);
            if (!t) { free(buf); return NULL; }
            buf = t; d = buf + off;
        }
        switch (*src) {
            case '"':  *d++ = '\\'; *d++ = '"';  break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n';  break;
            case '\r': *d++ = '\\'; *d++ = 'r';  break;
            case '\t': *d++ = '\\'; *d++ = 't';  break;
            default:   *d++ = *src; break;
        }
        src++;
    }
    *d = '\0';
    return buf;
}

/* 拆分 <think>...</think> 块，返回 malloc 的思考文本（或 NULL）。
 * 返回的字符串由调用方释放；输入字符串在原地修改以移除 think 块。 */
static char* ai_split_think(char *text)
{
    if (!text) return NULL;

    LV_LOG_USER("AI: split_think start, first 60 chars: %.60s", text);

    size_t tcap = 256, tlen = 0;
    char *think = malloc(tcap);
    if (!think) return NULL;
    think[0] = '\0';

    char *src = text;
    char *dst = text;
    bool hit_break = false;  /* true → memmove 已处理 NUL */

    while (*src) {
        /* Use strstr to find next <think> anywhere in remaining text */
        char *tag = strstr(src, "<think>");
        if (!tag) {
            /* No more think blocks — copy rest and finish */
            size_t rest = strlen(src);
            if (dst != src) memmove(dst, src, rest + 1);
            LV_LOG_USER("AI: split_think no more tags, remaining=%zu", rest);
            hit_break = true;
            break;
        }

        LV_LOG_USER("AI: split_think found <think> at offset %d", (int)(tag - text));

        /* Copy text before <think> */
        if (tag > dst) {
            size_t prefix = (size_t)(tag - src);
            if (dst != src) memmove(dst, src, prefix);
            dst += prefix;
        }
        src = tag + 7;  /* skip <think> */

        /* Find closing </think> */
        char *close_tag = strstr(src, "</think>");
        if (close_tag) {
            size_t chunk = (size_t)(close_tag - src);
            LV_LOG_USER("AI: split_think found </think>, chunk=%zu", chunk);

            /* Expand think buffer if needed */
            if (tlen + chunk + 2 > tcap) {
                tcap = tlen + chunk + 256;
                char *tmp = realloc(think, tcap);
                if (!tmp) { free(think); return NULL; }
                think = tmp;
            }

            if (tlen > 0) think[tlen++] = '\n';
            memcpy(think + tlen, src, chunk);
            tlen += chunk;
            think[tlen] = '\0';

            src = close_tag + 8;  /* skip </think> */
        } else {
            LV_LOG_USER("AI: split_think no closing </think> found");
            src += strlen(src);
        }
    }

    if (!hit_break && dst != text)
        *dst = '\0';

    LV_LOG_USER("AI: split_think done, think=%zu chars, answer=%zu chars",
                tlen, strlen(text));

    if (tlen == 0) { free(think); return NULL; }
    return think;
}

/* ── AI： 对话历史管理 ── */

static void ai_hist_add(const char *role, const char *msg)
{
    char *safe = ai_json_esc(msg);
    if (!safe) return;
    char frag[8192];
    snprintf(frag, sizeof(frag), "{\"role\":\"%s\",\"content\":\"%s\"}", role, safe);
    free(safe);

    if (g_ai_hist_n >= AI_HIST_MAX) {
        free(g_ai_hist[g_ai_hist_head]);
        g_ai_hist_head = (g_ai_hist_head + 1) % AI_HIST_MAX;
        g_ai_hist_n--;
    }
    g_ai_hist[(g_ai_hist_head + g_ai_hist_n) % AI_HIST_MAX] = strdup(frag);
    g_ai_hist_n++;
}

static void ai_hist_clear(void)
{
    for (int i = 0; i < AI_HIST_MAX; i++) {
        free(g_ai_hist[i]); g_ai_hist[i] = NULL;
    }
    g_ai_hist_n = g_ai_hist_head = 0;
}

/* ── AI： 从对话历史构建 JSON 请求体 ── */

static char* ai_build_body(const char *user_msg)
{
    /* Add user message to history for body building */
    ai_hist_add("user", user_msg);

    size_t cap = 32768;
    char *body = malloc(cap);
    if (!body) return NULL;

    /* Escaped system prompt and user message */
    char *esc_sys = ai_json_esc(OLLAMA_SYSTEM_MSG);
    int pos = snprintf(body, cap,
        "{\"model\":\"%s\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"}",
        OLLAMA_MODEL, esc_sys ? esc_sys : "");
    free(esc_sys);

    if (g_ai_hist_n > 0) {
        pos += snprintf(body + pos, cap - (size_t)pos, ",");
    }

    for (int i = 0; i < g_ai_hist_n; i++) {
        pos += snprintf(body + pos, cap - (size_t)pos,
            "%s%s", g_ai_hist[(g_ai_hist_head + i) % AI_HIST_MAX],
            i < g_ai_hist_n - 1 ? "," : "");
    }
    pos += snprintf(body + pos, cap - (size_t)pos, "],\"stream\":false}");

    LV_LOG_USER("AI: body %d bytes", pos);
    return body;
}

/* ── AI： UI 更新回调（单次 lv_async_call 保证顺序！） ── */

typedef struct {
    char *thinking;   /* malloc'd, NULL if none */
    char *answer;     /* malloc'd, NULL if none */
    char *error;      /* malloc'd, NULL if none */
} ai_ui_packet_t;

static void ai_ui_show(void * data)
{
    ai_ui_packet_t *p = (ai_ui_packet_t *)data;
    LV_LOG_USER("AI: ui_show screen=%p err=%p think=%p ans=%p",
                (void*)g_ai_screen, (void*)p->error,
                (void*)p->thinking, (void*)p->answer);

    if (g_ai_screen == NULL) goto cleanup;

    if (p->error) {
        ai_chat_page_show_error(g_ai_screen, p->error);
    } else {
        /* All steps in guaranteed order within ONE callback */
        ai_chat_page_begin_response(g_ai_screen);
        if (p->thinking && p->thinking[0]) {
            ai_chat_page_append_thinking(g_ai_screen, p->thinking);
        }
        ai_chat_page_finish_thinking(g_ai_screen);
        if (p->answer && p->answer[0]) {
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

/* ── AI： 分配 UI 数据包（思考 + 回答 + 错误） ── */
static ai_ui_packet_t * ai_pkt_new(const char *thinking, const char *answer,
                                    const char *error)
{
    ai_ui_packet_t *p = lv_malloc_zeroed(sizeof(*p));
    if (!p) return NULL;
    if (thinking) p->thinking = strdup(thinking);
    if (answer)   p->answer   = strdup(answer);
    if (error)    p->error    = strdup(error);
    return p;
}

/* ── AI： 接收线程 — HTTP POST → 解析 JSON → 拆分 <think> → lv_async_call ── */
static void * ai_recv_thread(void * arg)
{
    const char *user_msg = (const char *)arg;

    LV_LOG_USER("AI: thread start, msg='%s'", user_msg);

    /* ── 意图预检测：常见硬件请求由 C 代码直接执行，AI 只管回复 ── */
    const char *aug_msg = user_msg;
    char *pre_result = NULL;
    if (strstr(user_msg, "随机") && (strstr(user_msg, "LED") || strstr(user_msg, "灯")))
        pre_result = ai_execute_action("led_random_on");
    else if (strstr(user_msg, "切换蜂鸣器") || strstr(user_msg, "蜂鸣器开关"))
        pre_result = ai_execute_action("buzzer_toggle");
    else if (strstr(user_msg, "加速度") || strstr(user_msg, "偏转角") || strstr(user_msg, "姿态"))
        pre_result = ai_execute_action("read_accel");
    else if (strstr(user_msg, "按键") || strstr(user_msg, "按钮"))
        pre_result = ai_execute_action("read_button");
    /* 更多意图可以在此扩展 */

    if (pre_result && pre_result[0] && strncmp(pre_result, "[错误]", 7) != 0) {
        LV_LOG_USER("AI: pre-execute intent, result=%s", pre_result);
        /* 把执行结果追加到用户消息中，AI 会在上下文中看到 */
        size_t nlen = strlen(user_msg) + strlen(pre_result) + 64;
        char *tmp = malloc(nlen);
        if (tmp) {
            snprintf(tmp, nlen, "%s\n(系统已执行: %s)", user_msg, pre_result);
            aug_msg = tmp;
        }
    }
    free(pre_result);

    /* Build JSON body */
    char *body = ai_build_body(aug_msg);
    if (aug_msg != user_msg) free((void*)aug_msg);
    if (!body) {
        LV_LOG_USER("AI: body build failed");
        ai_ui_packet_t *p = ai_pkt_new(NULL, NULL, "构建请求失败");
        lv_async_call(ai_ui_show, p);
        g_ai_running = false;
        free((void*)user_msg);
        return NULL;
    }

#ifdef __linux__
    LV_LOG_USER("AI: POST to %s:%d", OLLAMA_HOST, OLLAMA_PORT);

    HttpResponse *r = http_post(OLLAMA_HOST, OLLAMA_PORT, "/api/chat", body, OLLAMA_TMO);
    free(body);

    if (g_ai_stop) {
        LV_LOG_USER("AI: stopped by user");
        ai_ui_packet_t *p = ai_pkt_new(NULL, NULL, "已停止生成");
        lv_async_call(ai_ui_show, p);
        g_ai_running = false;
        g_ai_stop = false;
        free((void*)user_msg);
        if (r) http_response_free(r);
        return NULL;
    }

    ai_ui_packet_t *pkt = lv_malloc_zeroed(sizeof(*pkt));
    if (!pkt) {
        http_response_free(r);
        g_ai_running = false;
        free((void*)user_msg);
        return NULL;
    }

    if (!r) {
        LV_LOG_USER("AI: http_post returned NULL");
        pkt->error = strdup("网络请求失败");
        lv_async_call(ai_ui_show, pkt);
        g_ai_running = false;
        g_ai_stop = false;
        free((void*)user_msg);
        return NULL;
    }

    if (r->status_code < 0) {
        LV_LOG_USER("AI: network error: %s", r->errmsg);
        char err[300];
        snprintf(err, sizeof(err), "网络错误: %s", r->errmsg);
        pkt->error = strdup(err);
        lv_async_call(ai_ui_show, pkt);
        http_response_free(r);
        g_ai_running = false;
        g_ai_stop = false;
        free((void*)user_msg);
        return NULL;
    }

    if (r->status_code != 200) {
        LV_LOG_USER("AI: HTTP %d, body=%.200s", r->status_code,
                    r->body ? r->body : "(null)");
        char err[512];
        const char *body_hint = "";
        if (r->body) {
            char *err_msg = ai_json_val(r->body, "error");
            body_hint = err_msg ? err_msg : "";
        }
        snprintf(err, sizeof(err), "服务器错误 HTTP %d%s%s",
                 r->status_code,
                 body_hint[0] ? ": " : "",
                 body_hint[0] ? body_hint : "");
        pkt->error = strdup(err);
        if (body_hint[0]) free((void*)body_hint);
        lv_async_call(ai_ui_show, pkt);
        http_response_free(r);
        g_ai_running = false;
        g_ai_stop = false;
        free((void*)user_msg);
        return NULL;
    }

    LV_LOG_USER("AI: response %zu bytes", r->body_len);

    /* Extract answer from JSON */
    char *content = ai_json_val(r->body, "content");
    if (content) {
        LV_LOG_USER("AI: content len=%zu", strlen(content));
        LV_LOG_USER("AI: content text=%.120s", content);

        /* 十六进制 dump 前 60 字节（排查隐藏字符/BOM/格式问题） */
        char hex[256];
        int hlen = 0;
        for (int i = 0; i < 60 && content[i]; i++) {
            hlen += snprintf(hex + hlen, sizeof(hex) - hlen,
                             "%02X ", (unsigned char)content[i]);
        }
        LV_LOG_USER("AI: content hex(60)=%s", hex);

        /* 检查是否包含 <action> / <think> 标签 */
        LV_LOG_USER("AI: has_action=%d  has_think=%d",
                    strstr(content, "[ACTION]") != NULL,
                    strstr(content, "<think>")  != NULL);
    } else {
        LV_LOG_USER("AI: content is NULL!");
    }

    if (content && content[0]) {
        /* ── 1. 先拆分 think（让 answer 和 think 分离） ── */
        LV_LOG_USER("AI: step1 split_think, before: len=%zu", strlen(content));
        char *thinking = ai_split_think(content);
        LV_LOG_USER("AI: after split_think, content len=%zu, think=%s",
                    strlen(content),
                    thinking ? (thinking[0] ? "yes" : "empty") : "none");

        /* ── 2. 在 answer 中搜 [ACTION] ── */
        LV_LOG_USER("AI: step2 split_actions in answer");
        char *act_ans = ai_split_actions(content);
        LV_LOG_USER("AI: answer actions: %s", act_ans ? act_ans : "(none)");

        /* ── 3. 在 think 中只移除 [ACTION] 行，不执行（AI 在引用语法） ── */
        if (thinking && thinking[0]) {
            LV_LOG_USER("AI: step3 strip_actions in think");
            ai_strip_actions(thinking);
            LV_LOG_USER("AI: think after strip: len=%zu", strlen(thinking));
        }

        /* ── 4. 动作结果即 answer 中执行的结果 ── */
        char *action_results = act_ans;  /* 只有 answer 中执行的才是有效结果 */

        /* ── 5. 如果 think 内容被移空则释放 ── */
        if (thinking && !thinking[0]) {
            free(thinking);
            thinking = NULL;
        }
        if (thinking) {
            pkt->thinking = thinking;  /* transfer ownership */
        }

        /* ── 6. 去掉前导空白 ── */
        char *trimmed = content;
        while (*trimmed == '\n' || *trimmed == '\r'
               || *trimmed == ' '  || *trimmed == '\t')
            trimmed++;
        if (trimmed != content) {
            memmove(content, trimmed, strlen(trimmed) + 1);
            LV_LOG_USER("AI: trimmed %d leading whitespace chars",
                        (int)(trimmed - content));
        }

        /* ── 7. 剩余内容即为回答 ── */
        if (content[0]) {
            LV_LOG_USER("AI: answer len=%zu, text=%.80s",
                        strlen(content), content);
            pkt->answer = content;
        } else if (action_results && action_results[0]) {
            /* answer 为空但有动作结果 → 用动作结果当回答 */
            LV_LOG_USER("AI: answer empty, using action result as answer");
            pkt->answer = strdup(action_results);
            free(content);
        } else if (pkt->thinking && pkt->thinking[0]) {
            /* 从 think 里截取（最多 400 字）当答案 */
            size_t tlen = strlen(pkt->thinking);
            size_t show = tlen > 400 ? 400 : tlen;
            LV_LOG_USER("AI: answer empty, fallback from think (%zu chars)",
                        show);
            pkt->answer = malloc(show + 1);
            if (pkt->answer) {
                memcpy(pkt->answer, pkt->thinking, show);
                pkt->answer[show] = '\0';
            }
            free(content);
        } else {
            LV_LOG_USER("AI: no answer and no think!");
            pkt->error = strdup("模型未生成有效回答");
            free(content);
        }

        /* ── 8. 传感器结果注入历史（LED/蜂鸣器用户可见，不注入）── */
        if (action_results && action_results[0]) {
            if (strstr(action_results, "[水温]")        ||
                strstr(action_results, "[pH]")          ||
                strstr(action_results, "[加速度")       ||
                strstr(action_results, "[按键]")        ||
                strstr(action_results, "[溶解氧]")) {
                ai_hist_add("user", action_results);
                LV_LOG_USER("AI: sensor result injected into history");
            } else {
                LV_LOG_USER("AI: LED/buzzer result, skip history injection");
            }
        }
        free(action_results);
    } else {
        if (content) free(content);
        LV_LOG_USER("AI: no content in response");
        pkt->error = strdup("模型返回为空");
    }

    /* Add assistant reply to history (strip [ACTION] & <think>, no exec) */
    char *raw_content = ai_json_val(r->body, "content");
    if (raw_content) {
        ai_strip_actions(raw_content);  /* 只移除 [ACTION] 行，不执行 */
        char *think = ai_split_think(raw_content);  /* strip <think> */
        free(think);
        ai_hist_add("assistant", raw_content);
        free(raw_content);
    }

    http_response_free(r);

    /* SINGLE lv_async_call — guarantees order! */
    lv_async_call(ai_ui_show, pkt);

#else  /* PC / Windows stub */
    LV_LOG_USER("AI: PC stub simulated response");
    free(body);
    {
        ai_ui_packet_t *p = ai_pkt_new(
            "分析用户问题…\n检索知识库：水产养殖手册 v2.3\n匹配合适的回答模板…\n置信度：0.94",
            "这是模拟的AI回复。在Linux/ARM板卡上运行时会连接到真实的Ollama服务器。\n\n请确保：\n1. Ollama 服务已启动 (ollama serve)\n2. 已拉取模型 (ollama pull deepseek-r1:7b)\n3. 网络可达 " OLLAMA_HOST ":",
            NULL);
        lv_async_call(ai_ui_show, p);
    }
#endif

    g_ai_running = false;
    g_ai_stop = false;
    free((void*)user_msg);
    LV_LOG_USER("AI: thread done");
    return NULL;
}

/* ── AI： 公开 API（由 ai_chat_page.c 和 ui.c 调用） ── */

void app_action_ai_init(void)
{
    LV_LOG_USER("AI: Ollama client ready (host=%s, model=%s)", OLLAMA_HOST, OLLAMA_MODEL);
}

void app_action_ai_set_screen(lv_obj_t * screen)
{
    g_ai_screen = screen;
}

void app_action_ai_send(const char * message)
{
    if (g_ai_running) {
        LV_LOG_USER("AI: already running, ignoring send");
        return;
    }
    if (message == NULL || message[0] == '\0') return;

    LV_LOG_USER("AI: send '%s'", message);

    g_ai_running = true;
    g_ai_stop    = false;

    char *msg_copy = strdup(message);
    if (!msg_copy) {
        g_ai_running = false;
        ai_ui_packet_t *p1 = ai_pkt_new(NULL, NULL, "内存不足");
        lv_async_call(ai_ui_show, p1);
        return;
    }

    if (pthread_create(&g_ai_thread, NULL, ai_recv_thread, msg_copy) != 0) {
        free(msg_copy);
        g_ai_running = false;
        ai_ui_packet_t *p2 = ai_pkt_new(NULL, NULL, "线程创建失败");
        lv_async_call(ai_ui_show, p2);
        return;
    }
    pthread_detach(g_ai_thread);
}

void app_action_ai_stop(void)
{
    if (!g_ai_running) return;
    LV_LOG_USER("AI: stop requested");
    g_ai_stop = true;
}
