# FFmpeg Framebuffer Video Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the failed mplayer stdout pipeline with an FFmpeg rawvideo pipeline that draws video only inside the LVGL video page's middle framebuffer region.

**Architecture:** FFmpeg runs as an external board-side process that decodes `/root/videos/5_output.mp4`, scales frames to `768x305`, and pipes RGB24 rawvideo to a small `fb_writer_raw` executable. LVGL keeps owning the UI and exposes the video rectangle by making that region transparent while the writer updates only `/dev/fb0` rectangle `(16,65,768,305)`.

**Tech Stack:** C99, LVGL v9, Linux framebuffer `/dev/fb0`, POSIX process signals, FFmpeg CLI rawvideo output, CMake for PC simulator build.

## Global Constraints

- Target board is GEC6818 Linux.
- PC/Windows simulator must keep building without requiring FFmpeg or Linux framebuffer headers for the main app.
- LVGL screen size is `800x480`.
- Video rectangle is `x=16`, `y=65`, `w=768`, `h=305`.
- Keep SquareLine-style app entry: `ui_init()` remains the single UI entry point.
- Keep action callbacks centralized in `src/ui-smart-water/pages/app_actions.c` and `src/ui-smart-water/pages/app_actions.h`.
- Do not switch to full-screen mplayer overlay playback.
- Keep existing Ocean/Aquatic theme and current video page layout.
- Existing user changes in the working tree must be preserved.

---

## File Structure

- Create `src/ui-smart-water/fb_writer_raw.c`
  - Standalone Linux utility for the board.
  - Reads raw RGB24 frames from stdin.
  - Writes converted pixels into `/dev/fb0` only at `(16,65,768,305)`.
  - Not linked into the PC LVGL simulator executable.

- Modify `src/ui-smart-water/pages/app_actions.c`
  - Replace mplayer/PNM command construction with FFmpeg/rawvideo command construction on `__linux__`.
  - Rename video process state from mplayer-specific names to FFmpeg/video-process names.
  - Keep the public callback signatures unchanged.

- Modify `src/ui-smart-water/pages/app_actions.h`
  - No signature change expected.
  - Only update comments if needed.

- Modify `CMakeLists.txt`
  - Add an optional Linux-only `fb_writer_raw` executable target when Linux framebuffer headers are available.
  - Ensure the existing `main` target still excludes `fb_writer_raw.c`.

- Keep `src/ui-smart-water/pages/video-page/video_page.c` unchanged unless implementation reveals a redraw issue.
  - It already provides `video_page_set_video_active()` and transparent video area behavior.

---

### Task 1: Add Raw RGB Framebuffer Writer

**Files:**
- Create: `src/ui-smart-water/fb_writer_raw.c`
- Modify: none
- Test: compile standalone on Linux/GEC6818 with `gcc src/ui-smart-water/fb_writer_raw.c -o fb_writer_raw`

**Interfaces:**
- Consumes: raw RGB24 frames from stdin, exactly `768 * 305 * 3` bytes per frame.
- Produces: board executable `fb_writer_raw` that writes video pixels into `/dev/fb0` at `(16,65)`.

- [ ] **Step 1: Create `fb_writer_raw.c` with fixed geometry and framebuffer setup**

Write this complete file to `src/ui-smart-water/fb_writer_raw.c`:

```c
// ========== fb_writer_raw.c ==========
// GEC6818 framebuffer video sub-region writer.
// Reads raw RGB24 frames from stdin, converts to framebuffer pixels,
// and writes only to the LVGL video area of /dev/fb0.
//
// Usage:
//   ffmpeg -i VIDEO -vf scale=768:305 -f rawvideo -pix_fmt rgb24 -an - | fb_writer_raw

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#define FB_DEVICE      "/dev/fb0"
#define VID_X          16
#define VID_Y          65
#define VID_W          768
#define VID_H          305
#define RGB_FRAME_SIZE (VID_W * VID_H * 3)

static unsigned char * fb_ptr    = NULL;
static size_t          fb_size   = 0;
static int             fb_stride = 0;
static int             fb_bpp    = 0;
static int             fb_fd     = -1;

static int  fb_init(void);
static void fb_cleanup(void);
static int  read_exact(int fd, unsigned char * buf, size_t count);
static void rgb24_to_bgrx_row(const unsigned char * src, unsigned char * dst, int count);
static void rgb24_to_rgb565_row(const unsigned char * src, unsigned char * dst, int count);

int main(int argc, char ** argv)
{
    (void)argc;
    (void)argv;

    if(fb_init() != 0) return 1;

    fprintf(stderr,
            "fb_writer_raw: fb_stride=%d bpp=%d vid=%dx%d+%d+%d raw=RGB24\n",
            fb_stride, fb_bpp * 8, VID_W, VID_H, VID_X, VID_Y);

    unsigned char * frame_buf = malloc(RGB_FRAME_SIZE);
    unsigned char * row_buf   = malloc(VID_W * fb_bpp);
    if(frame_buf == NULL || row_buf == NULL) {
        fprintf(stderr, "fb_writer_raw: malloc failed\n");
        free(frame_buf);
        free(row_buf);
        fb_cleanup();
        return 1;
    }

    while(read_exact(STDIN_FILENO, frame_buf, RGB_FRAME_SIZE) == 0) {
        for(int row = 0; row < VID_H; row++) {
            unsigned char * fb_row_ptr = fb_ptr + (VID_Y + row) * fb_stride + VID_X * fb_bpp;
            const unsigned char * src_row = frame_buf + row * VID_W * 3;

            if(fb_bpp == 4) {
                rgb24_to_bgrx_row(src_row, row_buf, VID_W);
                memcpy(fb_row_ptr, row_buf, VID_W * 4);
            } else if(fb_bpp == 2) {
                rgb24_to_rgb565_row(src_row, row_buf, VID_W);
                memcpy(fb_row_ptr, row_buf, VID_W * 2);
            } else {
                fprintf(stderr, "fb_writer_raw: unsupported framebuffer bpp=%d\n", fb_bpp * 8);
                break;
            }
        }
    }

    free(frame_buf);
    free(row_buf);
    fb_cleanup();
    return 0;
}

static int fb_init(void)
{
    fb_fd = open(FB_DEVICE, O_RDWR);
    if(fb_fd < 0) {
        perror("fb_writer_raw: open " FB_DEVICE);
        return 1;
    }

    struct fb_var_screeninfo vinfo;
    if(ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("fb_writer_raw: ioctl FBIOGET_VSCREENINFO");
        fb_cleanup();
        return 1;
    }

    fb_bpp = (int)vinfo.bits_per_pixel / 8;
    if(fb_bpp != 4 && fb_bpp != 2) {
        fprintf(stderr, "fb_writer_raw: unsupported bits_per_pixel=%u\n", vinfo.bits_per_pixel);
        fb_cleanup();
        return 1;
    }

    fb_stride = (int)vinfo.xres_virtual * fb_bpp;
    fb_size   = (size_t)vinfo.yres_virtual * (size_t)fb_stride;

    if(VID_X + VID_W > (int)vinfo.xres || VID_Y + VID_H > (int)vinfo.yres) {
        fprintf(stderr,
                "fb_writer_raw: video rect %dx%d+%d+%d exceeds fb %ux%u\n",
                VID_W, VID_H, VID_X, VID_Y, vinfo.xres, vinfo.yres);
        fb_cleanup();
        return 1;
    }

    fb_ptr = (unsigned char *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fb_fd, 0);
    if(fb_ptr == MAP_FAILED) {
        fb_ptr = NULL;
        perror("fb_writer_raw: mmap");
        fb_cleanup();
        return 1;
    }

    return 0;
}

static void fb_cleanup(void)
{
    if(fb_ptr != NULL) {
        munmap(fb_ptr, fb_size);
        fb_ptr = NULL;
    }
    if(fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
}

static int read_exact(int fd, unsigned char * buf, size_t count)
{
    size_t total = 0;
    while(total < count) {
        ssize_t n = read(fd, buf + total, count - total);
        if(n == 0) return 1;
        if(n < 0) {
            perror("fb_writer_raw: read");
            return 1;
        }
        total += (size_t)n;
    }
    return 0;
}

static void rgb24_to_bgrx_row(const unsigned char * src, unsigned char * dst, int count)
{
    for(int i = 0; i < count; i++) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = 0;
        src += 3;
        dst += 4;
    }
}

static void rgb24_to_rgb565_row(const unsigned char * src, unsigned char * dst, int count)
{
    unsigned short * out = (unsigned short *)dst;
    for(int i = 0; i < count; i++) {
        unsigned char r = src[0];
        unsigned char g = src[1];
        unsigned char b = src[2];
        out[i] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        src += 3;
    }
}
```

- [ ] **Step 2: Compile the writer standalone on Linux or the board toolchain**

Run on a Linux/GEC6818 build environment:

```bash
gcc src/ui-smart-water/fb_writer_raw.c -o fb_writer_raw
```

Expected: command exits successfully and creates `fb_writer_raw`.

If cross-compiling, run the equivalent compiler command, replacing the compiler name with the actual board compiler:

```bash
arm-linux-gcc src/ui-smart-water/fb_writer_raw.c -o fb_writer_raw
```

Expected: command exits successfully and creates an ARM `fb_writer_raw` binary.

- [ ] **Step 3: Verify raw byte input contract without touching `/dev/fb0`**

Run this command on any Linux machine only to confirm the frame-size math used by FFmpeg:

```bash
python3 - <<'PY'
print(768 * 305 * 3)
PY
```

Expected output:

```text
702720
```

One raw RGB24 frame must be exactly `702720` bytes.

- [ ] **Step 4: Commit Task 1**

```bash
git add src/ui-smart-water/fb_writer_raw.c
git commit -m "feat: add raw framebuffer video writer" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

Expected: commit succeeds.

---

### Task 2: Add Optional CMake Target for `fb_writer_raw`

**Files:**
- Modify: `CMakeLists.txt:152-154`
- Test: `cmake --build build`

**Interfaces:**
- Consumes: `src/ui-smart-water/fb_writer_raw.c` from Task 1.
- Produces: optional `fb_writer_raw` build target on non-Windows platforms; keeps `main` target unchanged.

- [ ] **Step 1: Modify `CMakeLists.txt` after the `main` target is linked**

After this existing block:

```cmake
add_executable(main ${MAIN_SOURCES})
target_compile_definitions(main PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(main ${MAIN_LIBS})
```

insert:

```cmake
# Board helper: FFmpeg pipes RGB24 frames into this framebuffer writer.
# Keep it separate from the PC simulator main target.
if(UNIX AND NOT APPLE)
    add_executable(fb_writer_raw src/ui-smart-water/fb_writer_raw.c)
endif()
```

- [ ] **Step 2: Configure/build the existing PC simulator target**

Run from the repository root:

```bash
cmake --build build
```

Expected: `main` still builds. On Linux, `fb_writer_raw` also builds. On Windows, the new target is skipped because `UNIX AND NOT APPLE` is false.

- [ ] **Step 3: Commit Task 2**

```bash
git add CMakeLists.txt
git commit -m "build: add framebuffer writer target" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

Expected: commit succeeds.

---

### Task 3: Switch Linux Video Backend to FFmpeg Rawvideo

**Files:**
- Modify: `src/ui-smart-water/pages/app_actions.c:18-232`
- Modify: `src/ui-smart-water/pages/app_actions.h:18-21` only if comment wording is updated
- Test: compile existing app with `cmake --build build`

**Interfaces:**
- Consumes: `fb_writer_raw` executable available in board `PATH` or configured by `FB_WRITER_RAW_PATH`.
- Produces: unchanged public callbacks:
  - `void app_action_set_video_screen(lv_obj_t * screen);`
  - `void app_action_video_control(video_action_t action);`
  - `void app_action_video_seek(int32_t position);`

- [ ] **Step 1: Update defines in `app_actions.c`**

Replace the current `FB_WRITER_PATH` define block:

```c
/* fb_writer executable path */
#ifndef FB_WRITER_PATH
#define FB_WRITER_PATH  "fb_writer"
#endif
```

with:

```c
/* FFmpeg and raw framebuffer writer executable paths */
#ifndef FFMPEG_PATH
#define FFMPEG_PATH  "ffmpeg"
#endif

#ifndef FB_WRITER_RAW_PATH
#define FB_WRITER_RAW_PATH  "fb_writer_raw"
#endif
```

- [ ] **Step 2: Rename Linux process state variables**

Inside `#ifdef __linux__`, replace:

```c
static pid_t     g_mplayer_pid   = 0;
static int       g_video_pos     = 0;
static bool      g_video_playing = false;
static lv_obj_t * g_video_screen = NULL;
```

with:

```c
static pid_t     g_video_pid     = 0;
static int       g_video_pos     = 0;
static bool      g_video_playing = false;
static lv_obj_t * g_video_screen = NULL;
```

- [ ] **Step 3: Replace the Linux helper functions**

Replace the entire Linux helper section from:

```c
static void mplayer_start(int start_pos)
```

through the closing brace of:

```c
static void mplayer_seek(int delta_sec)
```

with this code:

```c
static void video_start(int start_pos)
{
    if(g_video_pid != 0) return;

    char cmd[768];
    snprintf(cmd, sizeof(cmd),
        FFMPEG_PATH " -ss %d "
        "-i \"%s\" "
        "-vf scale=%d:%d "
        "-f rawvideo -pix_fmt rgb24 "
        "-an -loglevel quiet - 2>/dev/null | "
        FB_WRITER_RAW_PATH " &",
        start_pos,
        VIDEO_PATH,
        VID_W, VID_H);

    LV_LOG_USER("Starting video pipeline: %s", cmd);
    int ret = system(cmd);

    if(ret == 0) {
        FILE * fp = popen("pidof ffmpeg", "r");
        if(fp) {
            char buf[32] = {0};
            if(fgets(buf, sizeof(buf), fp)) {
                g_video_pid     = (pid_t)atoi(buf);
                g_video_pos     = start_pos;
                g_video_playing = true;
                video_page_set_video_active(g_video_screen, true);
                video_page_set_play_state(g_video_screen, true);
                LV_LOG_USER("video pipeline started, ffmpeg pid=%d", (int)g_video_pid);
            }
            pclose(fp);
        }
    }
}

static void video_stop(void)
{
    if(g_video_pid == 0) return;

    LV_LOG_USER("Stopping ffmpeg pid=%d", (int)g_video_pid);
    kill(g_video_pid, SIGTERM);
    usleep(200000);
    g_video_pid     = 0;
    g_video_playing = false;
    video_page_set_play_state(g_video_screen, false);
    video_page_set_video_active(g_video_screen, false);
}

static void video_pause(void)
{
    if(g_video_pid == 0 || !g_video_playing) return;

    LV_LOG_USER("Pausing ffmpeg (SIGSTOP)");
    kill(g_video_pid, SIGSTOP);
    g_video_playing = false;
    video_page_set_play_state(g_video_screen, false);
}

static void video_resume(void)
{
    if(g_video_pid == 0 || g_video_playing) return;

    LV_LOG_USER("Resuming ffmpeg (SIGCONT)");
    kill(g_video_pid, SIGCONT);
    g_video_playing = true;
    video_page_set_play_state(g_video_screen, true);
}

static void video_seek_relative(int delta_sec)
{
    if(g_video_pid == 0) return;

    g_video_pos += delta_sec;
    if(g_video_pos < 0) g_video_pos = 0;

    LV_LOG_USER("Seeking to %d sec", g_video_pos);

    kill(g_video_pid, SIGTERM);
    usleep(100000);
    g_video_pid = 0;

    video_start(g_video_pos);
}
```

- [ ] **Step 4: Update `app_action_video_control()` to call the new helpers**

Replace the Linux implementation of `app_action_video_control()` with:

```c
void app_action_video_control(video_action_t action)
{
    switch(action) {
        case VIDEO_ACTION_PLAY_PAUSE:
            if(g_video_pid == 0) {
                video_start(g_video_pos);
            } else if(g_video_playing) {
                video_pause();
            } else {
                video_resume();
            }
            break;

        case VIDEO_ACTION_REWIND:
            video_seek_relative(-10);
            break;

        case VIDEO_ACTION_FAST_FORWARD:
            video_seek_relative(+10);
            break;

        case VIDEO_ACTION_VOLUME_UP:
            system("amixer set PCM 5%+ 2>/dev/null");
            break;

        case VIDEO_ACTION_VOLUME_DOWN:
            system("amixer set PCM 5%- 2>/dev/null");
            break;
    }
}
```

- [ ] **Step 5: Update `app_action_video_seek()` to use the new process variable**

Replace the Linux implementation of `app_action_video_seek()` with:

```c
void app_action_video_seek(int32_t position)
{
    int total_sec = 236;
    g_video_pos = (position * total_sec) / 1000;

    LV_LOG_USER("Seek bar: pos=%ld -> %d sec", (long)position, g_video_pos);

    if(g_video_pid != 0) {
        kill(g_video_pid, SIGTERM);
        usleep(100000);
        g_video_pid = 0;
    }
    video_start(g_video_pos);
}
```

- [ ] **Step 6: Add a stop-on-navigation helper if back navigation does not already stop video**

Search `src/ui-smart-water/ui.c` for `nav_to_home`. If it only loads the home screen and does not stop video, add a public function declaration in `app_actions.h`:

```c
void app_action_video_stop(void);
```

Add this Linux implementation before `app_action_video_control()`:

```c
void app_action_video_stop(void)
{
    video_stop();
}
```

Add this non-Linux stub before the Windows stub `app_action_video_control()`:

```c
void app_action_video_stop(void)
{
    LV_LOG_USER("Video: stop (stub)");
}
```

Then update `src/ui-smart-water/ui.c` `nav_to_home()` to call stop before loading home:

```c
static void nav_to_home(void)
{
    app_action_video_stop();
    if(g_home_screen) {
        lv_screen_load(g_home_screen);
    }
}
```

- [ ] **Step 7: Build the app**

Run:

```bash
cmake --build build
```

Expected: the PC simulator app builds successfully. If building on Windows, `fb_writer_raw` target is skipped and only `main` builds.

- [ ] **Step 8: Commit Task 3**

```bash
git add src/ui-smart-water/pages/app_actions.c src/ui-smart-water/pages/app_actions.h src/ui-smart-water/ui.c
git commit -m "feat: use ffmpeg rawvideo pipeline for video page" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

Expected: commit succeeds.

---

### Task 4: Board-Side Validation Commands

**Files:**
- Modify: none required
- Test: run commands on GEC6818 after cross-compiling and deploying FFmpeg and `fb_writer_raw`

**Interfaces:**
- Consumes: deployed `/usr/bin/ffmpeg` or `ffmpeg` in `PATH`, deployed `/usr/bin/fb_writer_raw` or `fb_writer_raw` in `PATH`, video file `/root/videos/5_output.mp4`.
- Produces: proof that FFmpeg emits raw bytes and the writer draws only the LVGL video rectangle.

- [ ] **Step 1: Confirm FFmpeg emits rawvideo bytes**

Run on the board:

```sh
ffmpeg -i /root/videos/5_output.mp4 \
  -vf scale=768:305 \
  -f rawvideo -pix_fmt rgb24 \
  -an -loglevel quiet - \
  | wc -c
```

Expected: output is greater than `0`. For a 1-frame video it should be at least `702720`. For normal videos it should be much larger.

- [ ] **Step 2: Confirm raw writer starts and sees framebuffer metadata**

Run on the board:

```sh
ffmpeg -i /root/videos/5_output.mp4 \
  -vf scale=768:305 \
  -f rawvideo -pix_fmt rgb24 \
  -an -loglevel quiet - \
  | fb_writer_raw
```

Expected stderr begins with a line like:

```text
fb_writer_raw: fb_stride=3200 bpp=32 vid=768x305+16+65 raw=RGB24
```

Expected visual result: video appears inside the middle `768x305` region, not full screen.

- [ ] **Step 3: Confirm LVGL page integration**

Run the LVGL app on the board and test:

1. Login with the known test account.
2. Open the home page.
3. Press `视频监控`.
4. Press play.
5. Confirm the placeholder text disappears.
6. Confirm video appears in the middle region.
7. Press pause.
8. Confirm video freezes and the play icon returns.
9. Press play again.
10. Confirm video resumes.
11. Press fast-forward and rewind.
12. Confirm playback restarts without leaving the video page.
13. Press `返回首页`.
14. Confirm FFmpeg stops and home page appears.

- [ ] **Step 4: Commit validation notes if a board README exists**

If the repository already contains a board deployment README, append the two validation commands above. If no such README exists, skip this documentation commit.

Expected commit command if a README was updated:

```bash
git add <board-readme-path>
git commit -m "docs: add ffmpeg video validation commands" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Self-Review

### Spec coverage

- FFmpeg rawvideo pipeline: Task 3.
- Raw framebuffer writer: Task 1.
- Optional build target: Task 2.
- LVGL transparent area preservation: Task 3 keeps `video_page_set_video_active()` calls and does not redesign `video_page.c`.
- Board validation: Task 4.
- Avoid full-screen mplayer overlay: all tasks remove mplayer pipeline use and never introduce full-screen playback.

### Placeholder scan

No task uses `TBD`, `TODO`, `implement later`, or unspecified edge handling. Every code-changing step includes concrete code.

### Type consistency

The plan uses existing public callback signatures from `app_actions.h`. New helper names are internal except `app_action_video_stop(void)`, which is explicitly added to the header and consumed by `ui.c`.
