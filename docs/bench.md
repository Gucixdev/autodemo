## Benchmark

`./bench` (no arguments, median of 3 runs, wall time, this machine 2026-11-16):

| what | time |
|---|---|
| `check` of a demo (parse only) | 1 ms |
| render a 0.5 s terminal demo (60x12) | 627 ms |
| render 3000 lines of scrolling output + `ls`, 2 s | 1308 ms |
| render a 2 s still demo with AutoCut | 670 ms |
| render a Tk app under `Window x11`, 3 s | 4249 ms |

The terminal renders are dominated by tmux start-up and the webp encoder, not by the demo length. `Window x11` adds
about 1.2 s for starting Xwayland and the settle wait (`CFG_GUI_SETTLE_MS`). Missing `tmux`, `tkinter` or `Xwayland`
skips that line. The render quality gate (practically empty screen) applies to bench demos too, which is why they print text.
