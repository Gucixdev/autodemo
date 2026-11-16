## GUI backend

`Window gui` + `Size 900x560` (pixels) + `Run PROGRAM`: autodemo listens on its own Wayland socket (private HOME, never your
compositor), the program draws into shared-memory buffers, autodemo composites them and injects keys, pointer and wheel
events itself. Only libc, works the same under Wayland and Xorg. The child gets `GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1`
(software wl_shm rendering only) and no `DISPLAY`.

What the compositor does:
- **Windows**: the first toplevel fills the page; more toplevels (dialogs, `set_parent` or not) are centred over their parent and get
  keyboard focus while they are the topmost one (the others lose "activated"). `xdg_surface.set_window_geometry` is honoured, so
  client-side shadows (GTK) are cropped instead of shifting the window.
- **Popups**: `xdg_popup` (menus, combo boxes, `<select>`, tooltips, submenus) placed by their `xdg_positioner` (anchor rect, anchor,
  gravity, offset, flip/slide constraint inside the page), `reposition`, `grab`. A grabbing popup owns keyboard focus; a mouse press
  outside the open popups sends `popup_done` and is not delivered (so it only closes the menu, as on a real desktop); Escape is
  delivered to the popup, which closes itself. Pointer focus is re-picked when a popup opens or closes under a still pointer.
- **Subsurfaces** to any depth with `place_above/below`, and **input regions** (`wl_surface.set_input_region`): a surface with an empty
  input region is skipped when picking the pointer target. This is what makes Firefox's web content clickable: Gecko's content
  subsurface is input-transparent and GTK owns the pointer; sending the pointer to the subsurface made GDK drop every event.
- **WaitFor** (below), `AUTODEMO_WL_TRACE=1` prints every request and event with the time in ms.

Limits: wl_shm rendering only, one client connection, US key layout by default (`Layout pl` below), only the left mouse button, no clipboard, scale 1, no
decoration protocol (toolkits draw their own title bars), `Window x11` (a rootful Xwayland, no window manager): the X program draws inside one fixed-size root window, so `WaitFor title` does not see its title; use `WaitFor settle` or `Sleep`. Clicks and keys reach it (tested with Tk).

### WaitFor in a gui demo

    WaitFor title "text" [timeout]     # until a window title contains text (any open toplevel, dialogs included)
    WaitFor settle 500ms [timeout]     # until the window picture has not changed for 500 ms

The timeout defaults to 30 s. Like the terminal `WaitFor`, a timeout is an error (exit 4, no file written; `--force` keeps it), and so is
the program exiting during the wait. Titles are what the program sets with `xdg_toplevel.set_title` (Firefox: the page title plus its
suffix, so match a part of it). Use `settle` after an action whose result has no title, for example after a menu opens. The
recording keeps running while you wait (AutoCut still shortens a long still picture).

### Layout

`Layout pl` (default `us`) sends the client an xkb keymap `pc+pl+inet(evdev)`; the client compiles it with its own xkeyboard-config, so
autodemo needs no xkbcommon. `Send "zażółć"` then types the Polish letters as AltGr (+Shift for capitals) plus the base key; the
other characters use the US key positions, which `pl` shares. Other layouts are rejected by `check`. Tested with `test/gui-qt-pl.demo`.

### Tests

`make test` runs `test/gui-qt.demo` and `test/gui-qt-pl.demo` (PySide6 app `test/qtapp.py`: click, menu popup, combo popup, dismissal, typing, modal dialog; every
step is asserted with `WaitFor title`) and `test/gui-firefox.demo` (a private Firefox profile run by `test/ffbox`: click in web
content and a `<select>` popup). `test/ffbox` starts the flatpak Firefox inside bubblewrap: read-only runtime, private HOME, the
compositor socket, no network, nothing of your real profile (`~/.var`) is visible. Both are skipped with a note when PySide6 or
Firefox+bwrap are missing; `AUTODEMO_TEST_FIREFOX=0` skips Firefox.

## config.h

`src/config.h` holds every default, rebuild after editing: **default output format** (`CFG_FORMAT` webp|mp4|webm, used when no `-o`/extension is given), webp quality/effort, mp4/webm ffmpeg args and fps, default `Loop`, GitHub size limits, quality-gate thresholds, speed, transition, window size/font/fps, font file lists,
window chrome (margin, title bar, corner radius, shadow, traffic-light colours), pointer/ripple/caption colours,
slide look, and (in `src/fx/theme.c`) the **themes** (16-colour palette + text/background/outer/title-bar colours; add a line and use it
with `Theme NAME`). A script overrides size, theme, font, fps, speed and transitions per demo.
