# Roadmap

Layout today: `.autodemo/examples/` holds everything (hand-written `NAME.demo`, generated `REPO/NAME.demo`, and the `.webp` next to each demo). `projects/run` is the one file that builds and demos the user's projects.

## Next: what the READMEs really show
- **Full, hand-written demos for lazytui and gtop** (the two programs that get a real README demo). Everything else gets only a short overview.
- Generated per-tool demos (`projects/run`) stay as they are for now: a **short, selected overview**, not everything. Anything long (hf, gh output) must be cut to a few chosen commands, never a full listing.
- 35 tools have no usage examples yet (no man EXAMPLES, nothing in a README): each needs `-c 'cmd'` lines picked by hand, and `-s 'setup'` for the files it works on. `gen` refuses to write a `--help` demo on purpose (that is the README's job). Safe ones first: findfat, todo, trash, morph, manwriter, fixmedia, metadata_cleaner, flatseal show, sniff -r. Skipped on purpose (touch the real system, need hardware/network/root): btth, wifi, cleaner, ezcontainers, hf, screenshot, scenario, sudo, cb/xcb/wlcb/shot.
- Stale generated demos in `.autodemo/examples/REPO/` made with the old `--help` mode (and their webp) should be deleted once their tool has a real `-c` demo.
- Decide whether generated `examples/REPO/*` is committed or git-ignored.

## Tooling
- `autodemo auto URL`: clone a GitHub link, build (sandbox with network, filesystem confined), run without network, write a short README loop, a long mp4 with slower typing and an `.srt`, and a `tutorial.md` for complex programs. No automatic YouTube upload (outward-facing: the user pastes the link). Check the current GitHub README media limits.
- Sandbox the render itself for programs that come from a URL (today only `gen`'s probe and `autodemo sandbox` are sandboxed).
- Terminal backend without tmux: own pty + terminal emulator (about 500-800 lines, risky for full-screen TUIs). Pending decision; until then tmux is a documented requirement and a missing tmux gives a clear error.
- One `delay` value instead of `Sleep` lines: AutoCut / stabilization decides how long a still picture stays.
- Opener for webp: `mpv --loop` (open question).

## Known gaps
- `Window x11` (rootful Xwayland inside the built-in compositor): Xwayland starts, the picture is wrong (stripes, no window content). Ideas: pixel/channel interpretation, window never mapped without a WM, damage not reported.
- gui backend: mouse clicks do not reach Firefox web content (keyboard works).
- frost demo is sparse; 2x scale for the matrix theme; MouseScroll untested with a mouse-reporting app; `.demomobile` (low priority).
- `gen -c`: the "N commands" label is fine, but captions are plain "Step N".

## Done
- Built-in Wayland compositor (gui backend), sandbox (`src/sandbox.c`), `gen` / `auto`, `projects/run`, source split into `src/` + `src/fx/`.
