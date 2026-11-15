CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
FT = $(shell pkg-config --cflags --libs freetype2)
WEBP = $(shell pkg-config --libs libwebp libwebpmux)
autodemo: src/autodemo.c src/fx/transition.c src/fx/theme.c src/fx/anim.c src/fx/draw.c src/fx/text.c src/fx/chrome.c src/parse.c src/term.c src/record.c src/input.c src/session.c src/exec.c src/gate.c src/cli.c src/gui.c src/gen.c src/sandbox.c src/config.h
	nice -n 19 $(CC) $(CFLAGS) -o $@ $< $(FT) $(WEBP) -lm
test: autodemo
	sh test/run
clean:
	rm -f autodemo
.PHONY: test clean
