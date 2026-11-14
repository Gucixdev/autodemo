# autodemo

Scripted, non-interactive demo recorder for GitHub READMEs. You write a small `*.demo` script,
autodemo drives the program (typed keys, simulated mouse pointer, captions, slides, transitions)
and renders an **animated WebP** (or mp4/webm). Written in C, links freetype + libwebp.

![ls demo](.autodemo/export/ls.webp)

## Status (honest)

| Part | State |
|---|---|
| Script parser, `check` | done, tested |
| TUI/CLI backend (private tmux + software renderer) | done, tested on `ls`, frost, lazytui |
| Animated WebP (libwebp anim encoder, frame dedup, lossless or lossy) | done |
| mp4 / webm via ffmpeg | done (needs ffmpeg) |
| `preview` (ASCII key screens) | done |
| GUI backend (headless Wayland compositor + wtype + grim) | NOT implemented; `window gui` errors out |
| Web backend (headless Firefox screenshots) | NOT implemented; `window web` errors out |
| Emacs mode | not started; the format is plain line-based on purpose |

Limits: wide (CJK) glyphs take one cell, no italics, no blinking cursor, no bold face for
non-ASCII fallback, text fonts are DejaVu (override the mono font with `AUTODEMO_FONT=/x.ttf`).
Recording is real time (a 10 s demo takes ~10 s). Per-run temp dirs `/tmp/autodemo-home-*`
are left behind (tiny); delete them yourself.

## Script syntax

Command names are case-insensitive (`Send`, `send`). One statement per line; optional `;` ends a statement
(several per line), `Name(a, b)` works like `Name a b`, `#` starts a comment, `"strings"` take `\n \" \e` escapes.
`Run`, `Setup`, `Cd`, `Env` take the rest of the line verbatim (shell code keeps its own `;`).
`Feature "name" { ... }` and `Cut { ... }` are blocks closed with `}` (or the flat `Feature` / `Cut`..`CutEnd` forms).

    Send("echo hi"); Sleep(500ms)
    Feature "scrolling" {
      PageScroll down 2
    }

**Header** (whole demo, position does not matter): `Size 100x30` (cells), `Theme dark|light|dracula|matrix`,
`Font 15`, `Fps 10`, `Title "text"`, `Window tui` (gui/web not implemented yet), `Env KEY=VALUE`, `Cd DIR`,
`Setup SHELL-CMD` (prepare fixtures in the private HOME), `Loop` (loop forever; default plays once),
`Speed N`, `Typing instant|human`, `AutoCut on|off|MS`, `Transition [in|out|both] EFFECT [ms]`.

**Timeline:**

| Command | |
|---|---|
| `Run CMD` | start the program in the terminal (a shell works: `Run bash -i`) |
| `Send "text" [ms]` | text appears at once, then a short pause (`CFG_SEND_PAUSE_MS`); `Typing human` types char by char (`ms` per char); `\n` is Enter |
| `SendKey Enter Down*3 C-c` | tmux key names; `NAME*N` repeats |
| `Sleep 500ms\|2s` | pause |
| `MouseMove x y [ms]` / `MouseClick` / `MouseHide` / `MouseShow` | simulated pointer (cells, eased); click sends SGR mouse press/release to apps with mouse reporting |
| `PageScroll up\|down [n]` / `MouseScroll up\|down [n]` | PageUp/PageDown, or mouse wheel at the pointer |
| `Caption "text" [top\|bottom]` / `Caption off` | overlay until switched off |
| `Slide "title" ["subtitle" [2s]]` | title card with animated build-up |
| `Transition ...` | effects: none fade fade-in fade-out zoom zoom-in zoom-out slide slide-in slide-out slide-left slide-right slide-up slide-down wipe (in = entering a slide, out = leaving) |
| `Cut` / `CutEnd`, `WaitFor "text" [timeout]` | drop loading screens from the video; wait until text is on screen (default 30 s) |
| `Feature "name"` | section; lines before the first Feature always run; `--feature NAME` renders one, `--split` one file each |
| `AutoCut on\|off\|MS` | a picture unchanged for longer than MS (default `CFG_AUTOCUT_MS`) stops costing video time |

## config.h

`src/config.h` holds every default, rebuild after editing: **default output format** (`CFG_FORMAT` webp|mp4|webm, used when no `-o`/extension is given), webp quality/effort, mp4/webm ffmpeg args and fps, default `Loop`, GitHub size limits, quality-gate thresholds, speed, transition, window size/font/fps, font file lists,
window chrome (margin, title bar, corner radius, shadow, traffic-light colours), pointer/ripple/caption colours,
slide look, and the **themes** (16-colour palette + text/background/outer/title-bar colours; add a line and use it
with `Theme NAME`). A script overrides size, theme, font, fps, speed and transitions per demo.

## Checks

`render` refuses to write a bad demo (exit 4, `--force` keeps it): program died, empty screen, inputs that changed
nothing. Warnings for sparse or static screens. GitHub check on the output: over 10 MiB is refused (exit 5, GitHub
does not show README media that big), over 5 MiB warns. Live UIs (gtop, frost) are better as a short `Loop`.

Output is `error` (red) / `warn` (yellow) / `ok` / `note` lines; plain when piped or `NO_COLOR` is set.

## Usage

    make
    ./autodemo check   x.demo
    ./autodemo render  x.demo [-o out.webp|mp4|webm] [-q N] [--snap MS frame.ppm] [-v]
    ./autodemo preview x.demo
    ./autodemo features x.demo             # list Feature names
    ./autodemo render  x.demo --feature NAME | --split   # one feature, or one file per feature
    ./autodemo export  ls.mp4 gtop features:page-scroll.webp         # NAME.fmt from .autodemo/examples -> .autodemo/export
    ./autodemo bulk export mp4 [NAME...]   # every demo (or the listed ones); GUI/web demos are skipped
    ./autodemo bulk check | preview | clean | list

Layout: demos live in `.autodemo/examples/*.demo`; `render x.demo` with no `-o` (or `-o mp4`) writes
`.autodemo/export/x.<fmt>` and creates the directories.

`-q 0` (default) is lossless; terminal frames are flat colour so lossless is already about as small
as lossy. `.mp4`/`.webm` output goes through ffmpeg. `--snap` dumps the frame shown at MS as PPM
(handy for looking at the result without a player).

Everything runs with a private `HOME` (and XDG dirs) and a private tmux server: your real configs and
sessions are never touched. `setup` lines prepare files in that HOME.

## Sizes (all lossless WebP unless noted)

| demo | size | length |
|---|---|---|
| .autodemo/examples/ls.demo (80x16) | ~46 KiB | 8.9 s |
| .autodemo/examples/frost.demo (100x28) | ~22 KiB | 5.6 s |
| .autodemo/examples/lazytui.demo (110x34, constantly animating UI) | ~550 KiB (webm VP9: ~290 KiB) | 10.5 s |

Static-ish terminals compress to tiny files because only changed regions of changed frames are stored;
a UI that animates every frame is the worst case.

## Design

    script -> Cmd list -> executor --tick--> tmux capture-pane -e -> ANSI/SGR grid
                                              -> software renderer (FreeType) layer
                                              -> overlays (caption, pointer) -> frame
                                              -> dedup -> WebPAnimEncoder / ffmpeg

Backend interface for phase 2: a backend turns input events into either a cell grid (tui) or a pixel
image (gui via `WLR_BACKENDS=headless` compositor + `wtype` + `grim`; web via headless Firefox);
the overlay/transition/encoder stages already work on plain RGBA images.

## Tests

    make test
