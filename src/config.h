/* config.h - autodemo defaults. Change them here and rebuild; a demo script overrides most with a header directive. */
#ifndef AUTODEMO_CONFIG_H
#define AUTODEMO_CONFIG_H

/* output */
#define CFG_FORMAT         "webp"  /* default output format when none is given: webp | mp4 | webm */
#define CFG_QUALITY        0       /* webp: 0 = lossless, 1..100 = lossy (-q N overrides) */
#define CFG_WEBP_METHOD    4       /* webp effort 0..6: higher = smaller file, slower render */
#define CFG_VIDEO_FPS      30      /* mp4/webm frame rate */
#define CFG_MP4_ARGS       "-c:v libx264 -crf 24 -preset slow -movflags +faststart"
#define CFG_WEBM_ARGS      "-c:v libvpx-vp9 -crf 34 -b:v 0"
#define CFG_LOOP           0       /* 1 = every demo loops without needing a `Loop` line */
/* GitHub size check on the output (MiB): refused / warning / note */
#define CFG_GH_MAX_MIB     10.0
#define CFG_GH_WARN_MIB    5.0
#define CFG_GH_NOTE_MIB    2.0
/* quality gate (percent of non-blank cells at the busiest moment) */
#define CFG_GATE_EMPTY_PCT 3.0     /* below: error */
#define CFG_GATE_SPARSE_PCT 10.0   /* below: warning */
#define CFG_GATE_MIN_STYLES 3      /* fewer distinct text styles: warning (no colour) */
#define CFG_TRANSITION     "fade"  /* default effect between a slide and the terminal (see fx.c for the names) */
#define CFG_TRANSITION_MS  300     /* its length */
#define CFG_FX_STEP_MS     40      /* one effect frame (25 fps); smaller = smoother and a bigger file */
#define CFG_AUTOCUT_MS     1500    /* a still picture longer than this stops costing video time (0 = off); `AutoCut on|off|MS` in a script */
#define CFG_SPEED          2.0     /* playback speed of typing, mouse, sleeps and slides; `Speed N` in a script or --speed N changes it */
#define CFG_SLIDE_MS       2000    /* how long a slide stays when the script gives no time */
#define CFG_ZOOM_AMOUNT    0.25    /* zoom effects: how far the picture scales (0.25 = 25 %) */
#define CFG_WIPE_SOFT      0.12    /* wipe effect: width of the soft edge, as a fraction of the picture */


/* ---- window defaults (a script's Size/Font/Theme/Fps lines override them) ---- */
#define CFG_COLS           100     /* terminal width in cells */
#define CFG_ROWS           30      /* terminal height in cells */
#define CFG_FPS            10      /* polling rate of the terminal while recording */
#define CFG_FONT_PX        15      /* terminal font size in pixels */
#define CFG_THEME          "dark"  /* default theme: a name from THEMES below */
#define CFG_TYPING         "instant" /* "instant": Send shows the text at once, then pauses; "human": types char by char (`Typing human` in a script) */
#define CFG_SEND_PAUSE_MS  650     /* instant mode: pause after each Send, before Speed */
#define CFG_SCROLL_PAUSE_MS 350    /* PageScroll / MouseScroll: pause after each step, before Speed */
#define CFG_TYPE_MS        55      /* human mode: ms per character (jittered +-30 %), before Speed */
#define CFG_MOVE_MS        500     /* MouseMove when the script gives no time */

/* ---- fonts: first file that exists wins; AUTODEMO_FONT=/x.ttf overrides the terminal font ---- */
#define CFG_FONT_MONO      {"/usr/share/fonts/TTF/DejaVuSansMono.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/liberation/LiberationMono-Regular.ttf", 0}
#define CFG_FONT_MONO_BOLD {"/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", "/usr/share/fonts/liberation/LiberationMono-Bold.ttf", 0}
#define CFG_FONT_SANS      {"/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 0}
#define CFG_FONT_SANS_BOLD {"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/liberation/LiberationSans-Bold.ttf", 0}
#define CFG_TITLE_PX       13      /* window title font size */
#define CFG_TITLE_ALPHA    200     /* window title opacity 0..255 */
#define CFG_CAPTION_PX     20      /* caption font size */

/* ---- window chrome (pixels) ---- */
#define CFG_MARGIN         14      /* space around the window */
#define CFG_TITLEBAR_H     30
#define CFG_PADDING        8       /* space between the title bar / border and the terminal text */
#define CFG_CORNER_R       8
#define CFG_SHADOW_DX      4
#define CFG_SHADOW_DY      8
#define CFG_SHADOW_COLOR   0x000000
#define CFG_SHADOW_ALPHA   60
#define CFG_DOT_R          6       /* traffic-light dots */
#define CFG_DOT_CLOSE      0xff5f57
#define CFG_DOT_MIN        0xfebc2e
#define CFG_DOT_MAX        0x28c840

/* ---- overlays ---- */
#define CFG_POINTER_FILL    0xffffff   /* mouse pointer body */
#define CFG_POINTER_OUTLINE 0x000000
#define CFG_RIPPLE_COLOR    0x58a6ff   /* click ripple */
#define CFG_CAPTION_BG      0x000000
#define CFG_CAPTION_BG_ALPHA 205
#define CFG_CAPTION_FG      0xffffff
#define CFG_SLIDE_GRADIENT  24         /* slide background: how much lighter the bottom gets (0 = flat) */
#define CFG_SLIDE_ACCENT    4          /* slide underline colour = this palette index of the theme (4 = blue) */

/* ---- themes: name, 16-colour ANSI palette (0..7 normal, 8..15 bright), default text, terminal background,
 *      outer background around the window, title-bar colour. Add your own line and use it with `Theme NAME`. ---- */
/* optional per-theme terminal font (path lists, "~/" = your home; bitmap fonts like BDF/PCF work): font, font_bold, font_px */
typedef struct { const char *name; uint32_t pal[16], fg, bg, outer, bar; const char *const *font, *const *font_bold; int font_px; uint32_t accent; } Theme;
/* accent (0 = none): window border, title text and slide underline colour */
#define CFG_BORDER_PX      2       /* width of the accent border */
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
