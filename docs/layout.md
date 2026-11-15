## Source layout

`src/autodemo.c` is only the includes, `say()` and `main()`. The rest:

- `src/fx/` - pictures and effects, no knowledge of the recorder: `transition.c` (blend effects), `theme.c` (colour themes), `draw.c` (Img + fill/disc/rrect/glow), `text.c` (FreeType fonts and text), `chrome.c` (window frame, cell grid, pointer, caption), `anim.c` (transitions and slide build-up on top of the recorder).
- `src/parse.c` - the `.demo` script: tokenizer, `;` layer, validation.
- `src/term.c` - ANSI text to cell grid, private tmux server.
- `src/record.c` - recorder state `G`, timing, frame output, layout/compositing. `src/gate.c` - quality gate.
- `src/input.c` - typing, keys, scroll, mouse. `src/session.c` - start the program. `src/exec.c` - header, estimate, `execute()`.
- `src/cli.c` - usage and the project commands (list/export/bulk/gen/sandbox).
- `src/gui.c` - built-in Wayland compositor. `src/sandbox.c`, `src/gen.c`. Defaults: `src/config.h`.

Everything is one translation unit via `#include`, so the Makefile lists the files as dependencies of `src/autodemo.c`.
