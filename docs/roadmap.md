# Roadmap

Layout today: `.autodemo/examples/` holds everything (hand-written `NAME.demo`, generated `REPO/NAME.demo`, and the `.webp` next to each demo). `projects/run` is the one file that builds and demos the user's projects.

## Next: what the READMEs really show
- **Full, hand-written demos for lazytui and gtop** (the two programs that get a real README demo). Everything else gets only a short overview.
- Generated per-tool demos (`projects/run`) stay as they are for now: a **short, selected overview**, not everything. Anything long (hf, gh output) must be cut to a few chosen commands, never a full listing.
- 35 tools have no usage examples yet (no man EXAMPLES, nothing in a README): each needs `-c 'cmd'` lines picked by hand, and `-s 'setup'` for the files it works on. `gen` refuses to write a `--help` demo on purpose (that is the README's job). Safe ones first: findfat, todo, trash, morph, manwriter, fixmedia, metadata_cleaner, flatseal show, sniff -r. Skipped on purpose (touch the real system, need hardware/network/root): btth, wifi, cleaner, ezcontainers, hf, screenshot, scenario, sudo, cb/xcb/wlcb/shot.
- Stale generated demos in `.autodemo/examples/REPO/` made with the old `--help` mode (and their webp) should be deleted once their tool has a real `-c` demo.
- Decide whether generated `examples/REPO/*` is committed or git-ignored.


podział na zone ze np 1 wideo per zone w jednym pliku pokazujac jeden feature?
nawet pełnometrażowe tutoriale w planach i guides cnie autoguide moze?

zoom autozoom 
demo regen
auto flow discovery
screenshots 
index.html z calym guide 
narrator whispercpp
smart highlighting
auto language 
browser
record and replay cut etc
visual flow builder templates etc 
dymki podswietlenia instrukcje przyciski slajdy next previous krok po kroku 
podglad dekstop/mobile/web i tablety
smart selectors 
retry system branching variables steo validation undo redo global actions versioning export import errorlogs 
self healing selectors instant replay reusable components 
mp3 mp4 webp giff tiff avif
png jpg

gui/tui czy co tam do tych visual scripting w rust chyba 

sqlite postgresql


inspiracje 

AutoDemo Demos-as-Code 
autodemo joshshiman




## Tooling
- `autodemo auto URL`: clone a GitHub link, build (sandbox with network, filesystem confined), run without network, write a short README loop, a long mp4 with slower typing and an `.srt`, and a `tutorial.md` for complex programs. No automatic YouTube upload (outward-facing: the user pastes the link). Check the current GitHub README media limits.
- Sandbox the render itself for programs that come from a URL (today only `gen`'s probe and `autodemo sandbox` are sandboxed).
- Terminal backend without tmux: own pty + terminal emulator (about 500-800 lines, risky for full-screen TUIs). Pending decision; until then tmux is a documented requirement and a missing tmux gives a clear error.
- One `delay` value instead of `Sleep` lines: AutoCut / stabilization decides how long a still picture stays.
- Opener for webp: `mpv --loop` (open question).

## Known gaps
- frost demo is sparse; 2x scale for the matrix theme; MouseScroll untested with a mouse-reporting app; `.demomobile` (low priority).
- `gen -c`: the "N commands" label is fine, but captions are plain "Step N".

## Done
- Built-in Wayland compositor (gui backend), sandbox (`src/sandbox.c`), `gen` / `auto`, `projects/run`, source split into `src/` + `src/fx/`.
