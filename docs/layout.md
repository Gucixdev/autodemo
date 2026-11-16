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

`proofs/shm_pool.lean`: Lean 4 model of the wl_shm pool lifetime in `src/gui.c` (buffers cut from a pool, `destroy` before the buffers are gone).
Proves a live buffer always finds its pool mapped, and that the old unmap-at-once rule breaks it. Check with `lean4/check` from the `bash` repo.

Sanitizer run: `cc -g -O1 -fsanitize=address,undefined -o /tmp/ad-asan src/autodemo.c $(pkg-config --cflags --libs freetype2 libwebp libwebpmux) -lm`, then `AUTODEMO_BIN=/tmp/ad-asan sh test/run` (clean on 2026-11-16, including Tk under Xwayland and Qt).
