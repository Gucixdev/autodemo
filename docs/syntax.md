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
`Chrome on|off` (off: no fake window frame, margin, shadow or title bar, the picture is the program and nothing else; with `Window gui` the program is also told to draw no title bar of its own, via xdg-decoration),
`Font 15`, `Fps 10`, `Layout us|pl` (gui keyboard), `Title "text"`, `Window tui|gui`, `Env KEY=VALUE`, `Cd DIR`,
`Setup SHELL-CMD` (prepare fixtures in the private HOME), `Loop` (loop forever; default plays once),
`Audio "file.mp3" [volume 0..4]` (mp4/webm only: the file is looked up next to the demo, looped under the video, faded out over the last 1.5 s; webp is silent and says so), `Speed N`, `Typing instant|human`, `AutoCut on|off|MS`, `Transition [in|out|both] EFFECT [ms]`.

**Timeline:**

| Command | |
|---|---|
| `Run CMD` | start the program in the terminal (a shell works: `Run bash -i`) |
| `Send "text" [ms]` | text appears at once, then a short pause (`CFG_SEND_PAUSE_MS`); `Typing human` types char by char (`ms` per char); `\n` is Enter |
| `SendKey Enter Down*3 C-c` | tmux key names; `NAME*N` repeats |
| `Sleep 500ms\|2s` | pause |
| `Clipboard "text"` | `Window gui` only: the text becomes the selection, so Ctrl+V in the app pastes it (up to 60000 bytes) |
| `MouseMove x y [ms]` / `MouseClick` / `MouseHide` / `MouseShow` | simulated pointer (cells, eased); click sends SGR mouse press/release to apps with mouse reporting |
| `PageScroll up\|down [n]` / `MouseScroll up\|down [n]` | PageUp/PageDown, or mouse wheel at the pointer |
| `Caption "text" [top\|bottom]` / `Caption off` | overlay until switched off |
| `Slide "title" ["subtitle" [2s]]` | title card with animated build-up |
| `Transition ...` | effects: none fade fade-in fade-out zoom zoom-in zoom-out slide slide-in slide-out slide-left slide-right slide-up slide-down wipe (in = entering a slide, out = leaving) |
| `Cut` / `CutEnd`, `WaitFor "text" [timeout]` | drop loading screens from the video; wait until text is on screen (default 30 s) |
| `WaitFor title "text" [timeout]` / `WaitFor settle 500ms [timeout]` | `Window gui` only: until a window title contains the text / until the window picture stops changing for that long. Timeout is an error like the terminal form. See [gui.md](gui.md) |
| `Feature "name"` | section; lines before the first Feature always run; `--feature NAME` renders one, `--split` one file each |
| `AutoCut on\|off\|MS` | a picture unchanged for longer than MS (default `CFG_AUTOCUT_MS`) stops costing video time |

## Recording by hand: `autodemo rec`

    autodemo rec OUT.demo [-s SETUP]... [--mouse] [--size COLSxROWS] [--force] [--] [CMD...]

Runs CMD (default: a clean bash) in the same private tmux and private HOME a render uses, in a pty on your terminal. You play the scenario;
`Ctrl+]` (or the program exiting) stops. The file gets `Size` (your terminal, at most 120x40; `--size` sets it), `Typing human`, `Run`,
and per input: `Send` for text (Enter is `\n`), `SendKey` for keys (arrows, Home/End, PageUp/Down, F1-F10, Tab, Backspace, Ctrl and Alt combos,
repeats as `Down*3`), and `Sleep` for every pause of 0.7 s or more (rounded to 0.1 s). `--mouse` turns tmux mouse on and records
clicks and wheel as `MouseMove` + `MouseClick` / `MouseScroll`. It never overwrites a file (`--force` does) and writes nothing if you typed nothing.
The result is a normal script: read it, trim the pauses, add `Caption`/`Slide`/`Audio`, then `check` and `render`.
The privacy gate applies to the replay like any other demo: a recorded screen that shows your login name is refused.
