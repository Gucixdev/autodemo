/* autodemo - scripted, non-interactive demo recorder -> animated WebP / mp4 / webm.
 * See README.md. Build: make. TUI backend = private tmux server + software renderer. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <limits.h>
#include <strings.h>
#include <dirent.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <webp/encode.h>
#include <webp/mux.h>
#include "fx/transition.c"
#include "fx/theme.c"


/* ------------------------------------------------------------ output ---- */
/* one place for messages: red error, yellow warning, green ok, dim note; plain text when not a terminal (or NO_COLOR) */
enum { LV_ERR, LV_WARN, LV_OK, LV_NOTE };
static int color_on = -1;
static void say(int lv, const char *fmt, ...) {
  if (color_on < 0) color_on = (isatty(2) || getenv("AUTODEMO_COLOR")) && !getenv("NO_COLOR");
  static const char *col[] = {"\033[1;31m", "\033[1;33m", "\033[32m", "\033[2m"};
  static const char *sym[] = {"error", "warn ", "ok   ", "note "};
  char m[2048]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
  size_t l = strlen(m); while (l && m[l - 1] == '\n') m[--l] = 0;
  if (color_on) fprintf(stderr, "%s%s\033[0m %s%s\033[0m\n", col[lv], sym[lv], lv == LV_ERR ? "\033[31m" : lv == LV_WARN ? "\033[33m" : "", m);
  else fprintf(stderr, "%s %s\n", sym[lv], m);
}

#include "parse.c"

#include "fx/draw.c"

#include "fx/text.c"

/* ------------------------------------------------------------ themes ---- */

#include "term.c"
#include "fx/chrome.c"

/* ---------------------------------------------------------- recorder ---- */
typedef struct {
  /* config */
  int cols, rows, fps, fontpx, loop, theme, backend, gui, x11, size_set, page_new; char title[128]; Img page;
  char env[16][256]; int nenv; char cwd[512];
  /* canvas */
  int W, H, M, TB, PAD, tx0, ty0;
  /* state */
  Grid gr, prev; int have_session, dirty;
  /* quality gate */
  int autocut; long still, ncut_ms; int cut, ncut_frames, in_feat, feat_seen; const char *only_feat; int human, idle, realtime, dead, dead_status, peak_cells, peak_styles, nstates, ninputs, force, in_run; uint64_t last_hash; char dead_screen[2048];
  Img layer, last, frame; int have_last;
  char caption[256]; int cap_top;
  int ptr_on; double px, py; int ripple;
  char trans[2][16]; int trans_ms[2]; double speed;
  long T; double sleep_scale; int preview;
  /* sinks */
  WebPAnimEncoder *enc; WebPAnimEncoderOptions eopt; WebPConfig wcfg; int lossy;
  FILE *ff; int ff_written, ff_fps; char outfmt; /* w webp, v video */
  struct { long ms; char *file; int done; } snap[8]; int nsnap;
  long nframes; char slide_title[256];
} G;

static void msleep(G *g, int ms) {
  if (ms <= 0) return;
  /* fast-forward: a screen that has not changed for a few polls is idle, so do not really wait (video time still advances) */
  if (!g->realtime && g->idle >= 3) return;
  ms = (int)(ms * g->sleep_scale);
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&ts, NULL);
}
static void tick(G *g, int ms); static void wait_ms(G *g, int ms); static int sp(G *g, int ms); static int jitter(int base); static void render_layer(G *g);
#include "gui.c"
static void write_ppm(const Img *im, const char *path) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
  for (int i = 0; i < im->w * im->h; i++) fwrite(im->p + i * 4, 1, 3, f);
  fclose(f);
}
static int vid_flush(G *g, long upto) {
  long target = upto * g->ff_fps / 1000; size_t sz = (size_t)g->last.w * g->last.h * 4;
  while (g->ff_written < target) { if (fwrite(g->last.p, 1, sz, g->ff) != sz) return -1; g->ff_written++; }
  return 0;
}
/* advance video time; a picture that stays unchanged for longer than AutoCut ms stops costing video time */
static void advance(G *g, int dur, int same) {
  if (!same) g->still = dur;
  else if (g->autocut > 0) {
    long room = g->autocut - g->still; if (room < 0) room = 0;
    g->still += dur;
    if (dur > room) { g->ncut_ms += dur - room; dur = (int)room; }
  }
  g->T += dur;
}
/* present image for dur ms: dedup identical frames, only changed ones reach the encoder */
static void put(G *g, const Img *im, int dur) {
  if (g->cut) return; /* Cut ... CutEnd: nothing is recorded, video time stands still */
  size_t sz = (size_t)im->w * im->h * 4;
  if (!g->have_last || memcmp(g->last.p, im->p, sz)) {
    for (int i = 0; i < g->nsnap; i++) if (!g->snap[i].done && g->have_last && g->snap[i].ms < g->T) { write_ppm(&g->last, g->snap[i].file); g->snap[i].done = 1; }
    if (g->ff && g->have_last) vid_flush(g, g->T);
    if (g->enc) {
      WebPPicture pic; WebPPictureInit(&pic); pic.width = im->w; pic.height = im->h; pic.use_argb = 1;
      WebPPictureImportRGBA(&pic, im->p, im->w * 4);
      if (!WebPAnimEncoderAdd(g->enc, &pic, (int)g->T, &g->wcfg)) say(LV_ERR, "webp add failed: %s\n", WebPAnimEncoderGetError(g->enc));
      WebPPictureFree(&pic);
    }
    memcpy(g->last.p, im->p, sz); g->have_last = 1; g->nframes++; advance(g, dur, 0); return;
  }
  advance(g, dur, 1);
}

/* ---- rendering ---- */
static void layout(G *g) {
  const Theme *t = &THEMES[g->theme];
  g->M = CFG_MARGIN; g->TB = CFG_TITLEBAR_H; g->PAD = CFG_PADDING;
  g->W = g->cols * cw + 2 * g->PAD + 2 * g->M; g->H = g->rows * ch + 2 * g->PAD + g->TB + 2 * g->M;
  if (g->gui) { g->PAD = 0; g->W = g->cols + 2 * g->M; g->H = g->rows + g->TB + 2 * g->M; } /* gui: cols x rows are pixels */
  g->W += g->W & 1; g->H += g->H & 1;
  g->tx0 = g->M + g->PAD; g->ty0 = g->M + g->TB + g->PAD;
  (void)t;
}
static void render_layer(G *g) {
  const Theme *t = &THEMES[g->theme]; Img *L = &g->layer;
  chrome_window(L, t, g->M, g->TB, g->title);
  if (g->gui) { gui_blit(g); return; }
  if (!g->have_session) return;
  chrome_grid(L, t, &g->gr, g->tx0, g->ty0);
}
static void compose(G *g) {
  Img *F = &g->frame; memcpy(F->p, g->layer.p, (size_t)F->w * F->h * 4);
  if (g->ptr_on) chrome_pointer(F, g->px, g->py, g->ripple);
  if (g->caption[0]) chrome_caption(F, g->caption, g->cap_top, g->M, g->TB);
}
static void present_dirty(G *g, int dur) {
  if (g->cut) return;
  if (g->dirty || !g->have_last) { compose(g); g->dirty = 0; put(g, &g->frame, dur); }
  else advance(g, dur, 1);
}

/* ---- capture ---- */

#include "gate.c"

/* ---- transitions and slides: src/fx/anim.c ---- */
static int sp(G *g, int ms) { if (ms <= 0) return 0; int v = (int)(ms / g->speed + 0.5); return v < 1 ? 1 : v; }
#include "fx/anim.c"

/* ---- input ---- */
static unsigned rng_state = 12345;
static int jitter(int base) { rng_state = rng_state * 1103515245u + 12345u; return base * (70 + (rng_state >> 16) % 61) / 100; }

/* instant mode: the text appears at once (newlines become Enter), then a short pause so the viewer can read it */
static int send_instant(G *g, const char *s) {
  char *buf = strdup(s), *seg = buf; int rc = 0;
  for (;;) {
    char *nl = strchr(seg, '\n'); if (nl) *nl = 0;
    if (*seg) {
      size_t l = strlen(seg); char *arg = malloc(l + 3); memcpy(arg, seg, l); arg[l] = 0;
      if (l && arg[l - 1] == ';') { arg[l - 1] = '\\'; arg[l] = ';'; arg[l + 1] = 0; } /* a trailing ; would be tmux's command separator */
      rc = TMUX("send-keys", "-t", "d", "-l", "--", arg); free(arg);
      if (rc) break;
    }
    if (!nl) break;
    rc = TMUX("send-keys", "-t", "d", "Enter"); if (rc) break;
    seg = nl + 1;
  }
  free(buf);
  if (rc) { say(LV_ERR, "tmux send-keys failed\n"); return -1; }
  g->idle = 0; tick(g, 40); wait_ms(g, sp(g, CFG_SEND_PAUSE_MS));
  return 0;
}

static int send_text(G *g, const char *s, int speed) {
  if (g->gui) return gui_text(g, s, speed);
  if (!g->human) return send_instant(g, s);
  for (const unsigned char *p = (const unsigned char *)s; *p;) {
    const unsigned char *q = p; utf8_next(&q);
    char ch_[8]; size_t n = q - p; memcpy(ch_, p, n); ch_[n] = 0; p = q;
    int rc;
    if (ch_[0] == '\n') rc = TMUX("send-keys", "-t", "d", "Enter");
    else rc = TMUX("send-keys", "-t", "d", "-l", "--", ch_[0] == ';' && !ch_[1] ? "\\;" : ch_); /* a lone ; is tmux's command separator */
    if (rc) { say(LV_ERR, "tmux send-keys failed\n"); return -1; }
    g->idle = 0;
    tick(g, ch_[0] == '\n' ? speed * 3 : jitter(speed));
  }
  return 0;
}
static int send_key(G *g, const char *spec) {
  if (g->gui) return gui_key(g, spec);
  g->idle = 0;
  char b[64]; strncpy(b, spec, 63); b[63] = 0; int rep = 1;
  char *star = strchr(b, '*'); if (star) { *star = 0; rep = atoi(star + 1); }
  for (int i = 0; i < rep; i++) {
    if (TMUX("send-keys", "-t", "d", b)) { say(LV_ERR, "tmux send-keys '%s' failed\n", b); return -1; }
    tick(g, rep > 1 ? 90 : 120);
  }
  return 0;
}

/* feature names compare case-insensitively with spaces = dashes, so `--feature "Mouse scroll"` and mouse-scroll are the same */
static void feat_slug(const char *in, char *out, size_t n) {
  size_t k = 0;
  for (; *in && k + 1 < n; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c)) out[k++] = (char)tolower(c); else if (k && out[k - 1] != '-') out[k++] = '-';
  }
  while (k && out[k - 1] == '-') k--;
  out[k] = 0;
}
static int feat_match(const char *a, const char *b) { char x[128], y[128]; feat_slug(a, x, sizeof x); feat_slug(b, y, sizeof y); return !strcmp(x, y); }

/* does the text appear on the current screen? */
static int grid_has(G *g, const char *needle) {
  char line[4096];
  for (int r = 0; r < g->gr.rows; r++) {
    size_t n = 0;
    for (int c = 0; c < g->gr.cols && n + 5 < sizeof line; c++) {
      uint32_t cp = g->gr.c[r * g->gr.cols + c].cp; if (!cp) cp = ' ';
      if (cp < 0x80) line[n++] = (char)cp;
      else if (cp < 0x800) { line[n++] = (char)(0xc0 | cp >> 6); line[n++] = (char)(0x80 | (cp & 63)); }
      else if (cp < 0x10000) { line[n++] = (char)(0xe0 | cp >> 12); line[n++] = (char)(0x80 | (cp >> 6 & 63)); line[n++] = (char)(0x80 | (cp & 63)); }
      else { line[n++] = (char)(0xf0 | cp >> 18); line[n++] = (char)(0x80 | (cp >> 12 & 63)); line[n++] = (char)(0x80 | (cp >> 6 & 63)); line[n++] = (char)(0x80 | (cp & 63)); }
    }
    line[n] = 0;
    if (strstr(line, needle)) return 1;
  }
  return 0;
}

/* PageScroll: PageUp/PageDown keys to the program (pagers, editors, TUIs) */
static int page_scroll(G *g, int up, int n) {
  if (g->gui) { for (int i = 0; i < n; i++) { if (gui_key(g, up ? "PageUp" : "PageDown")) return -1; wait_ms(g, sp(g, CFG_SCROLL_PAUSE_MS)); } return 0; }
  for (int i = 0; i < n; i++) {
    if (TMUX("send-keys", "-t", "d", up ? "PageUp" : "PageDown")) { say(LV_ERR, "tmux send-keys failed\n"); return -1; }
    g->idle = 0; tick(g, 120); wait_ms(g, sp(g, CFG_SCROLL_PAUSE_MS));
  }
  return 0;
}
/* MouseScroll: wheel notches (SGR mouse reporting) at the pointer cell, or the screen centre; the program must have mouse reporting on */
static int mouse_scroll(G *g, int up, int n) {
  if (g->gui) return gui_wheel(g, up, n);
  int col = g->cols / 2, row = g->rows / 2;
  if (g->ptr_on) { col = (int)((g->px - g->tx0) / cw); row = (int)((g->py - g->ty0) / ch); }
  if (col < 0) col = 0; if (row < 0) row = 0; if (col >= g->cols) col = g->cols - 1; if (row >= g->rows) row = g->rows - 1;
  for (int i = 0; i < n; i++) {
    char seq[48]; snprintf(seq, sizeof seq, "\033[<%d;%d;%dM", up ? 64 : 65, col + 1, row + 1);
    if (TMUX("send-keys", "-t", "d", "-l", "--", seq)) { say(LV_ERR, "tmux send-keys failed\n"); return -1; }
    g->idle = 0; tick(g, 70);
  }
  wait_ms(g, sp(g, CFG_SCROLL_PAUSE_MS));
  return 0;
}

static void pointer_move(G *g, double cx, double cy, int ms) {
  if (g->gui) { gui_move(g, cx, cy, ms); return; }
  double tx = g->tx0 + cx * cw + cw / 2, ty = g->ty0 + cy * ch + ch / 3;
  if (!g->ptr_on) { g->ptr_on = 1; g->px = tx + 60; g->py = ty + 40; }
  double sx = g->px, sy = g->py; int n = ms / 40; if (n < 1) n = 1;
  for (int i = 1; i <= n; i++) {
    double u = (double)i / n; u = u * u * (3 - 2 * u);
    g->px = sx + (tx - sx) * u; g->py = sy + (ty - sy) * u; g->dirty = 1; tick(g, ms / n);
  }
}
static void pointer_click(G *g) {
  if (g->gui) { gui_click(g); return; }
  int col = (int)((g->px - g->tx0) / cw), row = (int)((g->py - g->ty0) / ch);
  if (col >= 0 && row >= 0 && col < g->cols && row < g->rows) { /* SGR mouse press+release for apps that enabled mouse reporting */
    char seq[64]; snprintf(seq, sizeof seq, "\033[<0;%d;%dM\033[<0;%d;%dm", col + 1, row + 1, col + 1, row + 1);
    TMUX("send-keys", "-t", "d", "-l", "--", seq);
  }
  for (int i = 1; i <= 4; i++) { g->ripple = i; g->dirty = 1; tick(g, 50); }
  g->ripple = 0; g->dirty = 1; tick(g, 120);
}

/* ---- session ---- */
static int start_session(G *g, const char *cmd) {
  char *v[3];
  if (g->have_session) TMUX("kill-server");
  char geom_x[16], geom_y[16]; snprintf(geom_x, 16, "%d", g->cols); snprintf(geom_y, 16, "%d", g->rows);
  char full[4096]; int n = snprintf(full, sizeof full, "exec env");
  for (int i = 0; i < g->nenv; i++) n += snprintf(full + n, sizeof full - n, " '%s'", g->env[i]);
  snprintf(full + n, sizeof full - n, " sh -c '%s'", cmd);
  (void)v;
  char *argv[24]; int a = 0;
  argv[a++] = "new-session"; argv[a++] = "-d"; argv[a++] = "-s"; argv[a++] = "d"; argv[a++] = "-x"; argv[a++] = geom_x; argv[a++] = "-y"; argv[a++] = geom_y;
  if (g->cwd[0]) { argv[a++] = "-c"; argv[a++] = g->cwd; }
  argv[a++] = full; argv[a] = NULL;
  /* single-quote escaping of cmd is the script author's job; reject quotes cleanly */
  if (strchr(cmd, '\'')) {
    /* escape ' as '\'' */
    char esc[2048]; int e = 0; for (const char *p = cmd; *p && e < 2040; p++) { if (*p == '\'') { memcpy(esc + e, "'\\''", 4); e += 4; } else esc[e++] = *p; }
    esc[e] = 0; snprintf(full + n, sizeof full - n, " sh -c '%s'", esc);
  }
  if (tmux(argv, NULL)) { say(LV_ERR, "cannot start tmux session\n"); return -1; }
  g->have_session = 1;
  for (int i = 0; i < 40; i++) { msleep(g, 50); if (!capture(g)) break; }
  return 0;
}

/* ---- backends (interface for phase 2) ----
 * A backend turns "input events" into either a cell Grid (tui) or a pixel Img
 * (gui/web). Only tui is implemented; gui = headless wlroots compositor + wtype + grim,
 * web = headless Firefox screenshots. They are selected with `window gui|web`. */
static int backend_available(const char *name) {
  (void)name; return 1; /* gui and web are the same: a real program in the built-in compositor */
}

/* ------------------------------------------------------------- main ---- */
static void dump_ascii(G *g, const Cmd *c) {
  printf("--- t=%.1fs line %d: %s %s\n", g->T / 1000.0, c->line, c->op == C_RUN ? "run" : c->op == C_SLIDE ? "slide" : "wait", c->a[0] ? c->a[0] : "");
  if (!g->have_session || g->gui) return;
  for (int r = 0; r < g->gr.rows; r++) {
    int last = g->gr.cols - 1; while (last >= 0 && g->gr.c[r * g->gr.cols + last].cp <= ' ') last--;
    for (int k = 0; k <= last; k++) put_utf8(stdout, g->gr.c[r * g->gr.cols + k].cp ? g->gr.c[r * g->gr.cols + k].cp : ' ');
    putchar('\n');
  }
}

static int execute(G *g, Script *sc) {
  for (int i = 0; i < sc->n; i++) {
    Cmd *c = &sc->v[i]; int ms;
    if (c->op == C_FEATEND) { g->feat_seen = 0; g->in_feat = 1; continue; } /* back to always-run commands */
    if (c->op == C_FEATURE) { g->feat_seen = 1; g->in_feat = !g->only_feat || feat_match(c->a[0], g->only_feat); continue; }
    if (g->only_feat && g->feat_seen && !g->in_feat) continue; /* another feature's commands */
    switch (c->op) {
    case C_CUT: g->cut = 1; break;
    case C_CUTEND: g->cut = 0; g->dirty = 1; break;
    case C_PSCROLL: g->ninputs++; if (page_scroll(g, !strcmp(c->a[0], "up"), c->n > 1 ? atoi(c->a[1]) : 1)) return -1; break;
    case C_MSCROLL: g->ninputs++; if (mouse_scroll(g, !strcmp(c->a[0], "up"), c->n > 1 ? atoi(c->a[1]) : 3)) return -1; break;
    case C_WAITFOR: {
      if (g->gui) { say(LV_ERR, "line %d: WaitFor reads terminal text and needs Window tui (use Sleep with gui)\n", c->line); return -1; }
      int to = 30000; if (c->n == 2) dur_ms(c->a[1], &to);
      int rl = g->realtime, found = 0, waited = 0; g->realtime = 1;
      while (waited <= to) { if (capture(g) == 0 && grid_has(g, c->a[0])) { found = 1; break; } tick(g, 100); waited += 100; }
      g->realtime = rl;
      if (!found) { say(LV_ERR, "line %d: WaitFor \"%s\": not on screen after %.1fs\n", c->line, c->a[0], to / 1000.0); return -1; }
      break; }
    case C_RUN: if (g->gui ? gui_start(g, c->a[0]) : start_session(g, c->a[0])) return -1; g->in_run = 1; g->dirty = 1; if (!g->title[0]) snprintf(g->title, sizeof g->title, "%.100s", c->a[0]); render_layer(g); g->dirty = 1; tick(g, 200); if (g->preview) dump_ascii(g, c); break;
    case C_SETUP: {
      char *argv[] = {"sh", "-c", c->a[0], NULL};
      if (run_argv(argv, NULL)) { say(LV_ERR, "line %d: setup command failed\n", c->line); return -1; }
      break; }
    case C_TYPE: g->ninputs++; if (send_text(g, c->a[0], sp(g, c->n > 1 ? atoi(c->a[1]) : CFG_TYPE_MS))) return -1; break;
    case C_KEY: g->ninputs++; for (int k = 0; k < c->n; k++) if (send_key(g, c->a[k])) return -1; break;
    case C_WAIT: dur_ms(c->a[0], &ms); wait_ms(g, sp(g, ms)); if (g->preview) dump_ascii(g, c); break;
    case C_CAPTION:
      snprintf(g->caption, sizeof g->caption, "%s", !strcmp(c->a[0], "off") ? "" : c->a[0]);
      g->cap_top = c->n > 1 && !strcmp(c->a[1], "top"); g->dirty = 1; tick(g, 150); break;
    case C_SLIDE: {
      ms = CFG_SLIDE_MS; if (c->n == 3) dur_ms(c->a[2], &ms);
      if (g->have_session) capture(g);
      compose(g); Img A = img_new(g->W, g->H); memcpy(A.p, g->frame.p, (size_t)g->W * g->H * 4);
      Img S = img_new(g->W, g->H);
      if (CFG_SLIDE_REVEAL_MS > 0) { Img B = img_new(g->W, g->H); slide_bg(g, &B); transition(g, &A, &B, 0); free(B.p); slide_reveal(g, &S, c->a[0], c->n > 1 ? c->a[1] : "", sp(g, CFG_SLIDE_REVEAL_MS)); }
      else { render_slide(g, &S, c->a[0], c->n > 1 ? c->a[1] : ""); transition(g, &A, &S, 0); }
      put(g, &S, sp(g, ms));
      if (g->have_session) { capture(g); render_layer(g); }
      compose(g); transition(g, &S, &g->frame, 1); g->dirty = 1;
      if (g->preview) dump_ascii(g, c);
      free(A.p); free(S.p); break; }
    case C_SPEED: g->speed = atof(c->a[0]); break;
    case C_AUTOCUT: g->autocut = !strcmp(c->a[0], "off") ? 0 : !strcmp(c->a[0], "on") ? CFG_AUTOCUT_MS : atoi(c->a[0]); break;
    case C_TYPING: g->human = !strcmp(c->a[0], "human"); break;
    case C_TRANS: {
      int k = (!strcmp(c->a[0], "in") || !strcmp(c->a[0], "out") || !strcmp(c->a[0], "both")) ? 1 : 0;
      int lo = !strcmp(c->a[0], "out") ? 1 : 0, hi = !strcmp(c->a[0], "in") ? 0 : 1; /* in: [0,0]  out: [1,1]  both: [0,1] */
      int tms = CFG_TRANSITION_MS; if (c->n > k + 1) dur_ms(c->a[k + 1], &tms);
      for (int w = lo; w <= hi; w++) { snprintf(g->trans[w], sizeof g->trans[w], "%s", c->a[k]); g->trans_ms[w] = tms; }
      break; }
    case C_CMOVE: ms = CFG_MOVE_MS; if (c->n == 3) dur_ms(c->a[2], &ms); pointer_move(g, atof(c->a[0]), atof(c->a[1]), sp(g, ms)); break;
    case C_CCLICK: if (!g->ptr_on) { say(LV_ERR, "line %d: MouseClick before MouseMove\n", c->line); return -1; } pointer_click(g); break;
    case C_CHIDE: g->ptr_on = 0; g->dirty = 1; tick(g, 50); break;
    case C_CSHOW: g->ptr_on = 1; g->dirty = 1; tick(g, 50); break;
    default: break; /* header directives were applied earlier */
    }
  }
  if (g->cut) say(LV_WARN, "Cut without CutEnd: the end of the demo was not recorded\n");
  return 0;
}

static void apply_header(G *g, Script *sc) {
  g->cols = CFG_COLS; g->rows = CFG_ROWS; g->fps = CFG_FPS; g->fontpx = CFG_FONT_PX; g->theme = 0; g->backend = 0;
  for (int k = 0; k < NTHEMES; k++) if (!strcmp(THEMES[k].name, CFG_THEME)) g->theme = k;
  g->speed = CFG_SPEED; g->autocut = CFG_AUTOCUT_MS; g->loop = CFG_LOOP; g->human = !strcmp(CFG_TYPING, "human");
  for (int w = 0; w < 2; w++) { strcpy(g->trans[w], CFG_TRANSITION); g->trans_ms[w] = CFG_TRANSITION_MS; }
  for (int i = 0; i < sc->n; i++) {
    Cmd *c = &sc->v[i];
    switch (c->op) {
    case C_SIZE: sscanf(c->a[0], "%dx%d", &g->cols, &g->rows); g->size_set = 1; break;
    case C_THEME: for (int k = 0; k < NTHEMES; k++) if (!strcmp(c->a[0], THEMES[k].name)) g->theme = k; break;
    case C_FPS: g->fps = atoi(c->a[0]); break;
    case C_FONT: g->fontpx = atoi(c->a[0]); break;
    case C_LOOP: g->loop = 1; break;
    case C_TITLE: snprintf(g->title, sizeof g->title, "%s", c->a[0]); break;
    case C_WINDOW: g->backend = !strcmp(c->a[0], "tui") ? 0 : 1; g->gui = g->backend; g->x11 = !strcmp(c->a[0], "x11"); break;
    case C_ENV: if (g->nenv < 16) snprintf(g->env[g->nenv++], 256, "%s", c->a[0]); break;
    case C_CD: snprintf(g->cwd, sizeof g->cwd, "%s", c->a[0]); break;
    }
  }
  if (g->gui && !g->size_set) { g->cols = CFG_GUI_W; g->rows = CFG_GUI_H; }
}

static long estimate_ms(Script *sc) {
  long t = 0; int ms;
  for (int i = 0; i < sc->n; i++) {
    Cmd *c = &sc->v[i];
    switch (c->op) {
    case C_WAIT: dur_ms(c->a[0], &ms); t += ms; break;
    case C_SLIDE: ms = CFG_SLIDE_MS; if (c->n == 3) dur_ms(c->a[2], &ms); t += ms + 600; break;
    case C_TYPE: t += (long)strlen(c->a[0]) * (c->n > 1 ? atoi(c->a[1]) : 55); break;
    case C_KEY: t += 120 * c->n; break;
    case C_CMOVE: ms = CFG_MOVE_MS; if (c->n == 3) dur_ms(c->a[2], &ms); t += ms; break;
    case C_CCLICK: t += 320; break;
    case C_RUN: t += 200; break;
    }
  }
  return (long)(t / CFG_SPEED);
}

#include "cli.c"

int main(int argc, char **argv) {
  if (argc >= 5 && !strcmp(argv[1], "sandbox")) { /* sandbox WORKDIR net|nonet CMD : run CMD with a private view of the system */
    int rc = sandbox_run(argv[2], !strcmp(argv[3], "net"), argv[4], argc > 5 ? argv[5] : NULL); if (rc < 0) { say(LV_ERR, "sandbox unavailable here\n"); return 125; } return rc;
  }
  if (argc >= 3 && (!strcmp(argv[1], "gen") || !strcmp(argv[1], "auto"))) { /* gen PROGRAM [-d DIR] [-c COMMAND]... */
    char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) { perror("readlink"); return 1; }
    self[l] = 0; const char *od = NULL; char *cu[8]; int nc = 0;
    for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "-d") && i + 1 < argc) od = argv[++i]; else if (!strcmp(argv[i], "-c") && i + 1 < argc && nc < 8) cu[nc++] = argv[++i]; else return usage(); }
    return gen_cmd(self, argv[2], argv[1][0] == 'a', od, cu, nc);
  }
  if (argc >= 2 && (!strcmp(argv[1], "export") || !strcmp(argv[1], "bulk") || !strcmp(argv[1], "list"))) {
    char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) { perror("readlink"); return 1; }
    self[l] = 0; return project_cmd(self, argc, argv);
  }
  if (argc < 3) return usage();
  const char *mode = argv[1], *in = argv[2], *out = NULL; int q = CFG_QUALITY, split = 0;
  G *g = calloc(1, sizeof *g);
  for (int i = 3; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "-q") && i + 1 < argc) q = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) verbose = 1;
    else if (!strcmp(argv[i], "--force")) g->force = 1;
    else if (!strcmp(argv[i], "--feature") && i + 1 < argc) g->only_feat = argv[++i];
    else if (!strcmp(argv[i], "--split")) split = 1;
    else if (!strcmp(argv[i], "--realtime")) g->realtime = 1;
    else if (!strcmp(argv[i], "--speed") && i + 1 < argc) cli_speed = atof(argv[++i]);
    else if (!strcmp(argv[i], "--snap") && i + 2 < argc && g->nsnap < 8) { g->snap[g->nsnap].ms = atol(argv[++i]); g->snap[g->nsnap++].file = argv[++i]; }
    else return usage();
  }
  Script sc = {0};
  if (parse_script(in, &sc)) return 1;
  apply_header(g, &sc);
  if (cli_speed > 0) g->speed = cli_speed;
  if (!strcmp(mode, "features")) {
    int nf = 0; for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE) { printf("%s\n", sc.v[i].a[0]); nf++; }
    if (!nf) say(LV_NOTE, "%s has no Feature lines\n", in);
    return 0;
  }
  if (g->only_feat) { int f = 0; for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE && feat_match(sc.v[i].a[0], g->only_feat)) f = 1;
    if (!f) { say(LV_ERR, "%s has no feature '%s' (autodemo features %s)\n", in, g->only_feat, in); return 1; } }
  if (!strcmp(mode, "check")) {
    printf("%s: ok, %d commands, %dx%d cells, ~%.1fs\n", in, sc.n, g->cols, g->rows, estimate_ms(&sc) / 1000.0);
    return 0;
  }
  int render = !strcmp(mode, "render"), preview = !strcmp(mode, "preview");
  if (!render && !preview) return usage();
  char outbuf[1024];
  if (render) {
    if (out && (!strcmp(out, "webp") || !strcmp(out, "mp4") || !strcmp(out, "webm"))) { /* bare format name */
      snprintf(outbuf, sizeof outbuf, "%s", out); out = NULL; fmt_ext = strdup(outbuf);
    }
    if (!out) { /* convention: <project>/.autodemo/export/<name>.<fmt>, project = parent of .autodemo/examples */
      char dir[1024], base[256]; snprintf(dir, sizeof dir, "%s", in);
      char *sl = strrchr(dir, '/'); const char *bn = sl ? sl + 1 : dir; if (sl) *sl = 0; else strcpy(dir, ".");
      snprintf(base, sizeof base, "%s", bn); char *dot = strrchr(base, '.'); if (dot) *dot = 0;
      size_t dl = strlen(dir); const char *tail = "/examples";
      if (dl >= strlen(tail) && !strcmp(dir + dl - strlen(tail), tail)) dir[dl - strlen(tail)] = 0; /* .autodemo */
      else { strncat(dir, "/.autodemo", sizeof dir - strlen(dir) - 1); }
      char fs[140] = ""; if (g->only_feat) { char sl[128]; feat_slug(g->only_feat, sl, sizeof sl); snprintf(fs, sizeof fs, ".%s", sl); }
      snprintf(outbuf, sizeof outbuf, "%s/export/%s%s.%s", dir, base, fs, fmt_ext ? fmt_ext : CFG_FORMAT); out = outbuf;
    } else if (!strrchr(strrchr(out, '/') ? strrchr(out, '/') : out, '.')) {
      snprintf(outbuf, sizeof outbuf, "%s." CFG_FORMAT, out); say(LV_ERR, "no extension in -o, writing %s\n", outbuf); out = outbuf;
    }
    mkdir_p(out);
    if (split) { /* one render per Feature: NAME.FEATURE.ext next to the chosen output */
      char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1); if (l <= 0) { perror("readlink"); return 1; } self[l] = 0;
      char base[1024]; snprintf(base, sizeof base, "%s", out); char *dot = strrchr(base, '.'); const char *ext = dot ? dot : "." CFG_FORMAT; char extb[16]; snprintf(extb, sizeof extb, "%s", ext); if (dot) *dot = 0;
      int nf = 0, bad = 0, skipped = 0;
      for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE) {
        char sl[128], o2[1200], qs[16], ss[32]; feat_slug(sc.v[i].a[0], sl, sizeof sl); snprintf(o2, sizeof o2, "%s.%s%s", base, sl, extb);
        char *a[16]; int n = 0; a[n++] = self; a[n++] = "render"; a[n++] = (char *)in; a[n++] = "--feature"; a[n++] = sc.v[i].a[0]; a[n++] = "-o"; a[n++] = o2;
        if (q) { snprintf(qs, sizeof qs, "%d", q); a[n++] = "-q"; a[n++] = qs; }
        if (cli_speed > 0) { snprintf(ss, sizeof ss, "%g", cli_speed); a[n++] = "--speed"; a[n++] = ss; }
        if (g->force) a[n++] = "--force";
        a[n] = NULL; nf++;
        say(LV_NOTE, "feature %s\n", sc.v[i].a[0]);
        int rc = run_self(self, a); if (rc == 3) skipped++; else if (rc) bad++;
      }
      if (!nf) { say(LV_ERR, "--split: %s has no Feature lines\n", in); return 1; }
      say(bad ? LV_ERR : LV_OK, "split: %d feature(s), %d failed, %d skipped\n", nf, bad, skipped);
      return bad != 0;
    }
  }
  for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_WINDOW && !backend_available(sc.v[i].a[0])) return 3;
  if (fonts_init(g->fontpx, &THEMES[g->theme])) { say(LV_ERR, "no usable monospace TTF found (set AUTODEMO_FONT=/path.ttf)\n"); return 1; }
  layout(g);
  g->preview = preview; g->sleep_scale = preview ? 0.3 : 1.0;
  int gc = g->gui ? 1 : g->cols, gr_ = g->gui ? 1 : g->rows; /* gui: pixels, no cell grid */
  g->gr = (Grid){gc, gr_, 0, 0, 0, calloc(gc * gr_, sizeof(Cell))};
  g->prev = g->gr; g->prev.c = calloc(gc * gr_, sizeof(Cell));
  g->layer = img_new(g->W, g->H); g->last = img_new(g->W, g->H); g->frame = img_new(g->W, g->H);
  render_layer(g); g->dirty = 1;

  /* private tmux server + private HOME: never touches the real user's config/sessions */
  snprintf(tm_sock, sizeof tm_sock, "autodemo-%d", getpid());
  snprintf(tm_home, sizeof tm_home, "/tmp/autodemo-home-XXXXXX");
  if (!mkdtemp(tm_home)) { perror("mkdtemp"); return 1; }
  snprintf(tm_conf, sizeof tm_conf, "%s/tmux.conf", tm_home);
  FILE *cf = fopen(tm_conf, "w");
  fprintf(cf, "set -g status off\nset -g remain-on-exit on\nset -g escape-time 0\nset -g default-terminal xterm-256color\nset -g history-limit 2000\n");
  fclose(cf);
  char xdg[160];
  setenv("HOME", tm_home, 1); unsetenv("TMUX"); setenv("LANG", "C.UTF-8", 1); setenv("LC_ALL", "C.UTF-8", 1);
  snprintf(xdg, sizeof xdg, "%s/.config", tm_home); setenv("XDG_CONFIG_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/share", tm_home); setenv("XDG_DATA_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/state", tm_home); setenv("XDG_STATE_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.cache", tm_home); setenv("XDG_CACHE_HOME", xdg, 1);
  { /* $AUTODEMO_ROOT = project root (parent of .autodemo/examples), so demos can run ./bin/x without absolute paths */
    char rp[PATH_MAX], *sl;
    if (realpath(in, rp) && (sl = strrchr(rp, '/'))) {
      *sl = 0; size_t l = strlen(rp); const char *t = "/.autodemo/examples";
      if (l >= strlen(t) && !strcmp(rp + l - strlen(t), t)) rp[l - strlen(t)] = 0;
      setenv("AUTODEMO_ROOT", rp, 1);
    }
  }
  setenv("PS1", "$ ", 1); setenv("COLORTERM", "truecolor", 1);
  if (g->nenv < 16 && !getenv("AUTODEMO_KEEP_ENV")) {}

  const char *ext = out ? strrchr(out, '.') : NULL;
  if (render && ext && (!strcmp(ext, ".mp4") || !strcmp(ext, ".webm"))) {
    char cmd[1024]; g->ff_fps = CFG_VIDEO_FPS;
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -an %s -pix_fmt yuv420p '%s'", g->W, g->H, CFG_VIDEO_FPS,
             !strcmp(ext, ".mp4") ? CFG_MP4_ARGS : CFG_WEBM_ARGS, out);
    g->ff = popen(cmd, "w"); if (!g->ff) { say(LV_ERR, "ffmpeg not available\n"); return 1; }
    g->outfmt = 'v';
  } else if (render) {
    for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_LOOP) g->loop = 1; /* before the encoder exists, or the loop flag is lost */
    WebPAnimEncoderOptionsInit(&g->eopt); g->eopt.anim_params.loop_count = g->loop ? 0 : 1; g->eopt.minimize_size = 1; g->eopt.allow_mixed = 1;
    WebPConfigInit(&g->wcfg); g->wcfg.lossless = q == 0; g->wcfg.quality = q ? q : 90; g->wcfg.method = CFG_WEBP_METHOD;
    if (q == 0) g->wcfg.exact = 0;
    g->enc = WebPAnimEncoderNew(g->W, g->H, &g->eopt); g->outfmt = 'w';
  }

  int rc = execute(g, &sc);
  if (g->gui) gui_close(g);
  else if (g->have_session) TMUX("kill-server");
  if (rc) return 1;
  int bad = gate_verdict(g);
  if (bad && !g->force) {
    say(LV_ERR, "not writing %s (%d problem%s). Fix the demo, or pass --force to keep it.\n", out ? out : "output", bad, bad == 1 ? "" : "s");
    return 4;
  }
  /* hold the last frame briefly so loops do not snap */
  present_dirty(g, 600);
  for (int i = 0; i < g->nsnap; i++) if (!g->snap[i].done && g->have_last) write_ppm(&g->last, g->snap[i].file);
  if (g->ff) { vid_flush(g, g->T); if (pclose(g->ff)) { say(LV_ERR, "ffmpeg failed\n"); return 1; } }
  if (g->enc) {
    WebPAnimEncoderAdd(g->enc, NULL, (int)g->T, NULL);
    WebPData data; WebPDataInit(&data);
    if (!WebPAnimEncoderAssemble(g->enc, &data)) { say(LV_ERR, "webp assemble failed\n"); return 1; }
    FILE *f = fopen(out, "wb"); if (!f || fwrite(data.bytes, 1, data.size, f) != data.size) { perror(out); return 1; }
    fclose(f); WebPDataClear(&data); WebPAnimEncoderDelete(g->enc);
  }
  if (render) {
    FILE *f = fopen(out, "rb"); long sz = 0; if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
    printf("%s: %dx%d, %.1fs, %ld unique frames, %.1f KiB\n", out, g->W, g->H, g->T / 1000.0, g->nframes, sz / 1024.0); if (g->ncut_ms > 0) say(LV_NOTE, "AutoCut removed %.1fs of still picture\n", g->ncut_ms / 1000.0);
    /* GitHub check: README media is inlined up to ~10 MB; big files are slow to load and bloat the repo */
    double mib = sz / 1048576.0;
    if (mib > CFG_GH_MAX_MIB && !g->force) { say(LV_ERR, "github: %.1f MiB is over the 10 MiB README media limit, it will not be shown. Shorten (Speed, Loop a live UI), shrink Size/Font, use -q 60 or mp4/webm. Output removed (--force keeps it).\n", mib); unlink(out); return 5; }
    if (mib > CFG_GH_MAX_MIB) say(LV_ERR, "github: %.1f MiB is over the 10 MiB README media limit\n", mib);
    else if (mib > CFG_GH_WARN_MIB) say(LV_WARN, "github: %.1f MiB is heavy for a README (limit 10 MiB); consider -q 60 or mp4/webm\n", mib);
    else if (mib > CFG_GH_NOTE_MIB) say(LV_NOTE, "github: %.1f MiB (fine, but slow on mobile)\n", mib);
  }
  return 0;
}
