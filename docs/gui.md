## GUI backend

`Window gui` + `Size 900x560` (pixels) + `Run PROGRAM`: autodemo listens on its own Wayland socket (private HOME, never your
compositor), the program draws into shared-memory buffers, autodemo composites them and injects keys, pointer and wheel
events itself. Only libc, works the same under Wayland and Xorg. Limits: software (wl_shm) rendering only (the child gets
`GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1`, `DISPLAY` is unset), one toplevel, no popups, no XWayland, US key layout,
no `WaitFor` (use `Sleep`), real-time capture. Knobs: `CFG_GUI_W/H/WAIT_MS/SETTLE_MS`. `AUTODEMO_WL_TRACE=1` prints requests.

## config.h

`src/config.h` holds every default, rebuild after editing: **default output format** (`CFG_FORMAT` webp|mp4|webm, used when no `-o`/extension is given), webp quality/effort, mp4/webm ffmpeg args and fps, default `Loop`, GitHub size limits, quality-gate thresholds, speed, transition, window size/font/fps, font file lists,
window chrome (margin, title bar, corner radius, shadow, traffic-light colours), pointer/ripple/caption colours,
slide look, and (in `src/fx/theme.c`) the **themes** (16-colour palette + text/background/outer/title-bar colours; add a line and use it
with `Theme NAME`). A script overrides size, theme, font, fps, speed and transitions per demo.
