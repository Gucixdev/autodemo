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
`Font 15`, `Fps 10`, `Title "text"`, `Window tui|gui`, `Env KEY=VALUE`, `Cd DIR`,
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
