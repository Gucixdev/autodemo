/* record.c - the recorder: state G, timing, frame output (dedup, AutoCut, encoder feed), window layout and compositing.
 * Includes the gui backend because that needs G. */
/* ---------------------------------------------------------- recorder ---- */
typedef struct {
  /* config */
  int cols, rows, fps, fontpx, loop, theme, backend, gui, x11, size_set, page_new; char title[128], layout[16]; Img page;
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
static int sp(G *g, int ms) { if (ms <= 0) return 0; int v = (int)(ms / g->speed + 0.5); return v < 1 ? 1 : v; }
