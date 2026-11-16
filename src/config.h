/* config.h - autodemo defaults. Change them here and rebuild; a demo script overrides most with a header directive. */
#ifndef AUTODEMO_CONFIG_H
#define AUTODEMO_CONFIG_H

/* gui backend (built-in Wayland compositor): window size in pixels, waits in ms */
#define CFG_GUI_W          900
#define CFG_GUI_H          560
#define CFG_GUI_WAIT_MS    30000   /* how long to wait for the program's first window */
#define CFG_GUI_SETTLE_MS  600     /* after the window appears, before recording */
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
#define CFG_TRANSITION     "fade"  /* default effect between a slide and the terminal (see src/fx/transition.c for the names) */
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
#define CFG_THEME          "dark"  /* default theme: a name from THEMES in src/fx/theme.c */
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
#define CFG_SLIDE_GLOW      78         /* slide: strength of the two soft light spots (0 = flat background) */
#define CFG_SLIDE_DOTS      22         /* slide: spacing of the faint dot grid in px (0 = none) */
#define CFG_SLIDE_BAR_W     84         /* slide: width of the accent pill under the title */
#define CFG_SLIDE_REVEAL_MS 500        /* slide: title/pill/subtitle build-up animation (0 = slide appears finished) */
#define CFG_SLIDE_ACCENT    4          /* slide underline colour = this palette index of the theme (4 = blue) */

#define CFG_BORDER_PX      2       /* width of the accent border */

/* themes live in src/fx/theme.c (add your own line there and use it with `Theme NAME`) */

#endif
