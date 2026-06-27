// ========== app_actions.c ==========
// Stub implementations — replace with real logic for your platform.
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

/*********************
 *      DEFINES
 *********************/
/* Video folder — change these to match your deployment layout */
#ifdef __linux__
#define VIDEOS_DIR      "/root/videos"
#define VIDEOS_DIR_FMT  "/root/videos/%s"
#else
#define VIDEOS_DIR      "A:../src/ui-smart-water/videos"
#define VIDEOS_DIR_FMT  "A:../src/ui-smart-water/videos/%s"
#endif

/* Video sub-region geometry (must match mplayer -geometry and video_page.c) */
#define VID_X      16
#define VID_Y      65
#define VID_W      768
#define VID_H      305

/* ===== VIDEO LIST — dynamically scanned ===== */
#define MAX_VIDEOS 5
static char ** g_video_paths  = NULL;
static int     g_video_count  = 0;
static int     g_video_index  = 0;
static char    g_cur_video_path[256] = "";

/**********************
 *  STATIC VARIABLES
 **********************/
static char g_saved_user[33] = "";
static char g_saved_pass[33] = "";

static lv_obj_t * g_video_screen = NULL;

#ifdef __linux__
static pid_t     g_mplayer_pid   = 0;
static int       g_video_pos     = 0;
static int       g_video_total_sec = 0;
static bool      g_video_playing = false;
static lv_timer_t * g_progress_timer = NULL;
static int       g_mp_fifo_fd    = -1;
#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/* ---- Login verification ---- */
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

/* ---- Registration ---- */
bool app_action_register_submit(const char * username, const char * password)
{
    LV_LOG_USER("Register attempt: user='%s'", username);
    snprintf(g_saved_user, sizeof(g_saved_user), "%s", username);
    snprintf(g_saved_pass, sizeof(g_saved_pass), "%s", password);
    return true;
}

void app_action_login_success(void)
{
    LV_LOG_USER("Login success!");
}

/* Called by ui.c after video page is created */
void app_action_set_video_screen(lv_obj_t * screen)
{
    g_video_screen = screen;
}

/* ---- Scanning: discover .mp4 files in videos/ folder ---- */
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

    /* Count .mp4 files (max 256 iterations, max MAX_VIDEOS) */
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

    /* Collect filenames */
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

    /* Init current path */
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

    /* Always stop any running mplayer before switching */
    app_action_video_stop();

    g_video_index = index;
    snprintf(g_cur_video_path, sizeof(g_cur_video_path), "%s", g_video_paths[index]);
#ifdef __linux__
    g_video_total_sec = 0;
#endif

    LV_LOG_USER("video_select: now playing %s", g_cur_video_path);

    /* Update UI — reset progress */
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

    /* Already cached? */
    if(access(cover_path, F_OK) == 0) {
        char cached[300];
        snprintf(cached, sizeof(cached), "A:%s", cover_path);
        char * dup = lv_malloc(strlen(cached) + 1);
        if(dup) { strcpy(dup, cached); return dup; }
        return NULL;
    }

    /* Try mplayer: run in /tmp so 00000001.png lands there directly */
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
        /* Verify the file is readable and has content */
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

/**********************
 *   WiFi / Network Check
 **********************/

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

    /* --- Step 1: external (WAN) --- */
    {
        int ret = system("ping -c 1 -w 2 8.8.8.8 >/dev/null 2>&1");
        if (ret == 0) {
            LV_LOG_USER("wifi: WAN reachable → GREEN");
            return WIFI_STATUS_GREEN;
        }
    }

    /* --- Step 2: gateway (LAN) --- */
    {
        /* Try to read the default gateway from the routing table */
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

        /* If we got a gateway, ping it */
        if (gw[0] != '\0') {
            char cmd[64];
            snprintf(cmd, sizeof(cmd),
                     "ping -c 1 -w 2 %s >/dev/null 2>&1", gw);
            if (system(cmd) == 0) {
                LV_LOG_USER("wifi: WAN dead but LAN (%s) reachable → YELLOW", gw);
                return WIFI_STATUS_YELLOW;
            }
        }

        /* Fallback: try common gateway addresses */
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

    /* --- Step 3: nothing reachable --- */
    LV_LOG_USER("wifi: no network → RED");
    return WIFI_STATUS_RED;

#else
    /* PC / Windows stub — always report GREEN for UI simulation */
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

/* ---- FIFO helpers ---- */

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

/* Query actual video duration from mplayer -identify (cached on first call). */
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

/* Force-kill any mplayer process (zombie or not) */
static void mplayer_force_kill(void)
{
    /* Kill by PID if we have it */
    if(g_mplayer_pid != 0) {
        LV_LOG_USER("mplayer_force_kill: killing pid=%d", (int)g_mplayer_pid);
        kill(g_mplayer_pid, SIGKILL);
        usleep(100000);
        g_mplayer_pid = 0;
    }
    /* Also kill any stray mplayer processes */
    system("killall -9 mplayer 2>/dev/null");
    usleep(50000);

    /* Clean up FIFO and timer */
    mp_fifo_close();
    unlink(MP_CMD_FIFO);

    if(g_progress_timer) {
        lv_timer_delete(g_progress_timer);
        g_progress_timer = NULL;
    }

    g_video_playing = false;
    g_video_pos = 0;
}

/* Start mplayer in slave mode. ALWAYS kills old process first. */
static void mplayer_start(int start_pos)
{
    /* Force kill any existing mplayer — never have two running */
    mplayer_force_kill();

    LV_LOG_USER("mplayer_start: path=%s pos=%d", g_cur_video_path, start_pos);

    bool first_duration = (g_video_total_sec == 0);
    int total = get_video_duration_sec();
    if(total < 1) total = 236;

    /* Create the command FIFO */
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
        /* Wait a moment for mplayer to start */
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

/* Called when mplayer exits unexpectedly (EOF, crash, etc.) */
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
    /* Reset progress so user can replay */
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

/* ----- Controls: send slave commands over FIFO, NO kill+restart ----- */

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

/* ----- Public callbacks ----- */

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

/* ----- Progress timer ----- */

static void progress_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(!g_video_playing) return;

    /* Detect mplayer exit (EOF) — kill(pid, 0) returns -1 if process gone */
    if(g_mplayer_pid != 0 && kill(g_mplayer_pid, 0) != 0) {
        mplayer_died();
        return;
    }

    if(g_video_pos < g_video_total_sec) {
        g_video_pos++;
    }

    /* Flush UI every 2 ticks so the slider moves smoothly. */
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

#else  /* !__linux__ — PC / Windows stub */

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

#endif /* __linux__ */

/**********************
 *   Network Communication
 **********************/
/**********************
 *   Network Communication — socket-integrated
 *   socket → connect → spawn recv_thread → LVGL-safe UI updates
 **********************/
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
static pthread_t g_recv_tid;         /* Receive thread */
static bool      g_recv_run  = false; /* Thread run flag */

/* ---- Thread-safe: push received message to LVGL ---- */
static void tcp_update_screen(void * data)
{
    if (g_network_screen == NULL || data == NULL) return;
    char * msg = (char *)data;
    network_page_append_message(g_network_screen, NETWORK_MSG_RECV, msg);
    lv_free(msg);
}

/* ---- Receive thread ---- */
static void * tcp_recv_thread(void * arg)
{
    (void)arg;
    char buf[TCP_BUF_SIZE];

    while (g_recv_run) {
        int n = read(g_sock, buf, sizeof(buf) - 1);
        if (n <= 0) {
            /* Connection closed or error */
            lv_async_call(tcp_update_screen, strdup("服务器已断开"));
            break;
        }
        buf[n] = '\0';
        /* Strip trailing \n */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (len > 1 && buf[len - 2] == '\r') buf[len - 2] = '\0';

        /* Copy to heap — lv_async_call will free it */
        lv_async_call(tcp_update_screen, strdup(buf));
    }

    /* Cleanup on thread exit */
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    g_recv_run = false;
    /* Update UI */
    lv_async_call(tcp_update_screen, strdup("[client] 连接已断开"));
    return NULL;
}

/* ---- Connect ---- */
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

/* ---- Disconnect ---- */
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

#endif /* __linux__ */

/* ================================================================ */

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
