# FFmpeg Rawvideo Framebuffer Video Design

Date: 2026-06-25

## Goal

Display video inside the existing LVGL smart-water video page without full-screen mplayer framebuffer takeover. The video should appear only in the middle player region while the LVGL top bar, progress bar, and control buttons remain visible and styled.

Target video region:

- `x = 16`
- `y = 65`
- `w = 768`
- `h = 305`

The solution targets the GEC6818 Linux board. The PC/Windows simulator keeps using stubs.

## Problem

The current mplayer pipeline cannot feed decoded frames to `fb_writer` on the GEC6818 board. The tested mplayer build supports framebuffer output for full-screen playback, but its available video output drivers do not provide a usable stdout frame stream:

- `rawvideo` is not available.
- `pnm` writes image files instead of pipe data.
- `yuv4mpeg` writes `stream.yuv` or blocks with FIFO/stdout variants.

Because the current mplayer is user-cross-compiled, one option is rebuilding it with different outputs. However, the more deterministic path is to cross-compile FFmpeg and use its rawvideo stdout support.

## Selected Approach

Use FFmpeg to decode and scale the video, then pipe raw RGB24 frames into a dedicated framebuffer sub-region writer.

Playback command shape:

```sh
ffmpeg -ss <start_pos> \
  -i /root/videos/5_output.mp4 \
  -vf scale=768:305 \
  -f rawvideo \
  -pix_fmt rgb24 \
  -an -loglevel quiet - \
  | fb_writer_raw &
```

Responsibilities:

- FFmpeg decodes the video file, scales frames to `768x305`, and writes raw RGB24 frames to stdout.
- `fb_writer_raw` reads fixed-size frames from stdin and writes only the configured rectangle in `/dev/fb0`.
- LVGL remains responsible for page layout, controls, navigation, and visual styling.

## Components

### `fb_writer_raw.c`

Add a new framebuffer writer for raw RGB24 input.

Behavior:

1. Open `/dev/fb0`.
2. Read framebuffer metadata with `FBIOGET_VSCREENINFO`.
3. `mmap` the framebuffer.
4. Allocate one RGB24 frame buffer of `VID_W * VID_H * 3` bytes.
5. Loop reading exactly one full frame at a time from stdin.
6. Convert each row into the framebuffer format:
   - 32 bpp: RGB24 to BGRX.
   - 16 bpp: RGB24 to RGB565.
7. Copy converted rows into `/dev/fb0` at `(VID_X, VID_Y)`.
8. Exit cleanly when stdin closes.

This file replaces the PPM-specific runtime path but does not need to delete the existing `fb_writer.c` immediately.

### `app_actions.c`

Change the Linux/GEC6818 video backend from mplayer+PNM to FFmpeg+rawvideo.

State remains similar to the current implementation:

- video process PID
- current playback position in seconds
- playing/paused state
- current video screen pointer

Control behavior:

- Play: start the FFmpeg pipeline from the current position.
- Pause: send `SIGSTOP` to the FFmpeg process.
- Resume: send `SIGCONT` to the FFmpeg process.
- Rewind/Fast-forward: adjust the stored position, stop the current FFmpeg process, restart with `-ss`.
- Seek bar: convert slider position to seconds, stop current process, restart with `-ss`.
- Return home: stop FFmpeg and restore the normal LVGL video placeholder state.
- Volume buttons: keep using `amixer` for now if board audio is configured separately.

The process lookup should use `pidof ffmpeg` or capture the launched shell pipeline PID carefully. If `pidof ffmpeg` is used, document that only one FFmpeg video playback instance should run on the board at a time.

### `video_page.c`

Keep the existing transparent video-area behavior.

When playback starts:

- `video_page_set_video_active(screen, true)` hides the placeholder labels.
- The LVGL background for the video screen becomes transparent where needed.
- Top bar, progress area, and control area keep explicit opaque backgrounds.

When playback stops:

- `video_page_set_video_active(screen, false)` restores the placeholder labels and normal background.

## FFmpeg Cross-Compilation Guidance

Build the smallest FFmpeg that can decode the target MP4 file and output rawvideo. A starting configuration is:

```sh
./configure \
  --prefix=/opt/ffmpeg-gec6818 \
  --arch=arm \
  --target-os=linux \
  --cross-prefix=arm-linux- \
  --enable-cross-compile \
  --disable-doc \
  --disable-debug \
  --disable-network \
  --disable-everything \
  --enable-ffmpeg \
  --enable-protocol=file \
  --enable-demuxer=mov \
  --enable-decoder=h264 \
  --enable-parser=h264 \
  --enable-filter=scale \
  --enable-muxer=rawvideo \
  --enable-encoder=rawvideo
```

If the video codec is not H.264, enable the matching decoder and parser. If the toolchain prefix differs from `arm-linux-`, use the board's actual cross compiler prefix.

## Validation Plan

Validate in stages on the GEC6818 board.

1. Confirm FFmpeg can output frame bytes:

   ```sh
   ffmpeg -i /root/videos/5_output.mp4 \
     -vf scale=768:305 \
     -f rawvideo -pix_fmt rgb24 \
     -an -loglevel quiet - \
     | wc -c
   ```

   Expected result: byte count is greater than zero.

2. Confirm `fb_writer_raw` can draw the video region:

   ```sh
   ffmpeg -i /root/videos/5_output.mp4 \
     -vf scale=768:305 \
     -f rawvideo -pix_fmt rgb24 \
     -an -loglevel quiet - \
     | fb_writer_raw
   ```

   Expected result: video appears only in the `768x305` middle region.

3. Confirm LVGL integration:

   - Login succeeds.
   - Home page opens.
   - Video page opens.
   - Play starts FFmpeg and hides placeholder text.
   - Pause freezes playback.
   - Resume continues playback.
   - Rewind/fast-forward restart near the expected position.
   - Return home stops playback.

## Risks and Mitigations

- FFmpeg binary may be large. Mitigation: use `--disable-everything` and enable only required demuxers, decoders, filters, and muxers.
- Software decoding may be CPU-heavy on GEC6818. Mitigation: test with `768x305`; if needed, lower the decode scale or reduce input bitrate.
- Audio is not handled by the rawvideo pipe. Mitigation: keep initial scope to video display and existing volume controls; add audio only after video is stable.
- `pidof ffmpeg` can affect multiple FFmpeg processes. Mitigation: assume one playback instance initially; improve process handling later if needed.
- LVGL may redraw over the video area. Mitigation: keep video area transparent and avoid invalidating that rectangle during playback except when controls change.

## Out of Scope

- Full-screen mplayer overlay playback.
- Hardware decoder integration.
- Perfect progress tracking from FFmpeg runtime output.
- Multi-video playlist management.
- General-purpose media player controls beyond the current UI.
