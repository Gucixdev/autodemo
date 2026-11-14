/* theme.c - colour themes: pure data, nothing from autodemo.c. */
#ifndef AUTODEMO_THEME_C
#define AUTODEMO_THEME_C
#include <stdint.h>

/* ---- themes: name, 16-colour ANSI palette (0..7 normal, 8..15 bright), default text, terminal background,
 *      outer background around the window, title-bar colour. Add your own line and use it with `Theme NAME`. ---- */
/* optional per-theme terminal font (path lists, "~/" = your home; bitmap fonts like BDF/PCF work): font, font_bold, font_px */
typedef struct { const char *name; uint32_t pal[16], fg, bg, outer, bar; const char *const *font, *const *font_bold; int font_px; uint32_t accent; } Theme;
/* accent (0 = none): window border, title text and slide underline colour */
static const char *const FONT_FIXED6X13[]  = {"~/.local/share/fonts/Fixed6x13.bdf", "/usr/share/fonts/misc/6x13.pcf.gz", 0};
static const char *const FONT_FIXED6X13B[] = {"/usr/share/fonts/misc/6x13B.pcf.gz", 0};
static const Theme THEMES[] = {
  {"dark", {0x1d1f21,0xcc6666,0xb5bd68,0xf0c674,0x81a2be,0xb294bb,0x8abeb7,0xc5c8c6,0x969896,0xde935f,0xb5bd68,0xf0c674,0x81a2be,0xb294bb,0x8abeb7,0xffffff}, 0xc5c8c6, 0x1d1f21, 0x2b2f3a, 0x30343f},
  {"light",{0x000000,0xc82829,0x718c00,0xeab700,0x4271ae,0x8959a8,0x3e999f,0xd6d6d6,0x4d4d4c,0xf5871f,0x718c00,0xeab700,0x4271ae,0x8959a8,0x3e999f,0xffffff}, 0x4d4d4c, 0xfafafa, 0xd8dce5, 0xe6e8ee},
  {"dracula",{0x21222c,0xff5555,0x50fa7b,0xf1fa8c,0xbd93f9,0xff79c6,0x8be9fd,0xf8f8f2,0x6272a4,0xff6e6e,0x69ff94,0xffffa5,0xd6acff,0xff92df,0xa4ffff,0xffffff}, 0xf8f8f2, 0x282a36, 0x1b1c26, 0x343746},
  /* matrix: green 00ff88 text on black, purple frame/title/accents, some yellow and blue; your Fixed6x13 bitmap font */
  {"matrix",{0x1a1a1a,0xcc0000,0x23fd00,0xffee33,0x3584e4,0x9944ff,0x33c7de,0x00ff88,0x5e5c64,0xf66151,0x5afe40,0xffff66,0x62a0ea,0xb77bff,0x5bc8fb,0xffffff}, 0x00ff88, 0x000000, 0x07040d, 0x140a24, FONT_FIXED6X13, FONT_FIXED6X13B, 13, 0x9944ff},
};
#define NTHEMES ((int)(sizeof THEMES / sizeof THEMES[0]))

#endif
