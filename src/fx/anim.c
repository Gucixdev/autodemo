/* anim.c - animated pictures built on the recorder: transitions between two pictures and the title-slide build-up.
 * Included by autodemo.c after G, Img, put() and the text drawing exist. Pure picture math lives in transition.c. */

/* which: 0 = entering a slide, 1 = leaving it */
static void transition(G *g, const Img *A, const Img *B, int which) {
  int fx = fx_parse(g->trans[which]), tms = sp(g, g->trans_ms[which]);
  if (fx <= FX_NONE || tms <= 0) { put(g, B, 0); return; }
  int n = tms / CFG_FX_STEP_MS; if (n < 1) n = 1;
  Img M = img_new(A->w, A->h);
  for (int i = 1; i <= n; i++) {
    double u = (double)i / n; u = u * u * (3 - 2 * u);
    fx_blend(M.p, A->p, B->p, A->w, A->h, fx, u);
    put(g, &M, CFG_FX_STEP_MS);
  }
  free(M.p);
}
/* soft radial light: alpha falls off quadratically from the centre */
static void img_glow(Img *im, int cx, int cy, int r, uint32_t c, int maxa) {
  for (int y = cy - r < 0 ? 0 : cy - r; y < im->h && y <= cy + r; y++)
    for (int x = cx - r < 0 ? 0 : cx - r; x < im->w && x <= cx + r; x++) {
      double d = sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy))) / r; if (d >= 1) continue;
      img_px(im, x, y, c, (int)(maxa * (1 - d) * (1 - d)));
    }
}
/* slide background: theme colour, two soft glows (accent top-left, text colour bottom-right), faint dot grid, corner brackets */
static void slide_bg(G *g, Img *S) {
  const Theme *t = &THEMES[g->theme]; uint32_t ac = t->accent ? t->accent : t->pal[CFG_SLIDE_ACCENT];
  img_fill(S, 0, 0, S->w, S->h, t->bg, 255);
  int r = S->w > S->h ? S->w : S->h;
  img_glow(S, S->w / 5, S->h / 4, r * 6 / 10, ac, CFG_SLIDE_GLOW);
  img_glow(S, S->w * 4 / 5, S->h * 4 / 5, r * 5 / 10, t->fg, CFG_SLIDE_GLOW / 2);
  if (CFG_SLIDE_DOTS) for (int y = 18; y < S->h; y += CFG_SLIDE_DOTS) for (int x = 18; x < S->w; x += CFG_SLIDE_DOTS) img_px(S, x, y, t->fg, 26);
  int in = 16, L = 26; /* corner brackets */
  for (int k = 0; k < 4; k++) {
    int x = (k & 1) ? S->w - in : in, y = (k & 2) ? S->h - in : in, dx = (k & 1) ? -1 : 1, dy = (k & 2) ? -1 : 1;
    img_fill(S, dx > 0 ? x : x - L, dy > 0 ? y : y - 2, L, 2, ac, 170);
    img_fill(S, dx > 0 ? x : x - 2, dy > 0 ? y : y - L, 2, L, ac, 170);
  }
}
/* title, accent pill and subtitle; p* = reveal progress 0..1 (fade + rise) */
static void slide_text(G *g, Img *S, const char *title, const char *sub, double pt, double pb, double ps) {
  const Theme *t = &THEMES[g->theme]; uint32_t ac = t->accent ? t->accent : t->pal[CFG_SLIDE_ACCENT];
  int tp = S->w / 9; if (tp > 76) tp = 76; if (tp < 22) tp = 22;
  while (tp > 16 && text_width(fsansb, tp, title) > S->w - 90) tp -= 2;
  int tw = text_width(fsansb, tp, title), has = sub && *sub;
  int cy = S->h / 2 - (has ? tp / 3 : 0);
  int ty = cy + tp / 3 + (int)((1 - pt) * 16);
  if (pt > 0) draw_text(S, fsansb, tp, (S->w - tw) / 2, ty, title, t->fg, (int)(255 * pt));
  int bw = (int)(CFG_SLIDE_BAR_W * pb); if (bw > 0) img_rrect(S, (S->w - bw) / 2, cy + tp - 2, bw, 5, 2, ac, 255);
  if (has && ps > 0) {
    int sp = tp * 2 / 5; if (sp < 13) sp = 13;
    int sw = text_width(fsans, sp, sub);
    draw_text(S, fsans, sp, (S->w - sw) / 2, cy + tp + sp + 22 + (int)((1 - ps) * 12), sub, t->fg, (int)(190 * ps));
  }
}
static void render_slide(G *g, Img *S, const char *title, const char *sub) { slide_bg(g, S); slide_text(g, S, title, sub, 1, 1, 1); }
/* background first (via the transition), then the text builds up: title rises in, bar grows, subtitle follows */
static void slide_reveal(G *g, Img *S, const char *title, const char *sub, int total_ms) {
  Img bg = img_new(S->w, S->h); slide_bg(g, &bg);
  int n = total_ms / CFG_FX_STEP_MS; if (n < 1) n = 1;
  for (int i = 1; i <= n; i++) {
    double u = (double)i / n, e = u * u * (3 - 2 * u);
    double pb = (u - 0.2) / 0.5, ps = (u - 0.45) / 0.55; pb = pb < 0 ? 0 : pb > 1 ? 1 : pb; ps = ps < 0 ? 0 : ps > 1 ? 1 : ps;
    memcpy(S->p, bg.p, (size_t)S->w * S->h * 4);
    slide_text(g, S, title, sub, e, pb * pb * (3 - 2 * pb), ps * ps * (3 - 2 * ps));
    put(g, S, CFG_FX_STEP_MS);
  }
  free(bg.p);
}

