/* chrome.c - what is drawn around and over the terminal: window frame, title bar, the cell grid, mouse pointer with click ripple,
 * caption bar. Functions on an Img and plain values; nothing from the recorder. Needs Theme, Img, Grid and the text functions. */

/* outer background, shadow, border, rounded window, title bar with the three dots and the centred title */
static void chrome_window(Img *L, const Theme *t, int M, int TB, const char *title) {
  img_fill(L, 0, 0, L->w, L->h, t->outer, 255);
  int ww = L->w - 2 * M, wh = L->h - 2 * M;
  img_fill(L, M + CFG_SHADOW_DX, M + CFG_SHADOW_DY, ww, wh, CFG_SHADOW_COLOR, CFG_SHADOW_ALPHA);
  int bp = t->accent ? CFG_BORDER_PX : 0;
  if (bp) img_rrect(L, M, M, ww, wh, CFG_CORNER_R, t->accent, 255);
  img_rrect(L, M + bp, M + bp, ww - 2 * bp, wh - 2 * bp, CFG_CORNER_R, t->bg, 255);
  img_rrect(L, M + bp, M + bp, ww - 2 * bp, TB - bp, CFG_CORNER_R, t->bar, 255);
  img_fill(L, M + bp, M + TB - CFG_CORNER_R, ww - 2 * bp, CFG_CORNER_R, t->bar, 255);
  if (bp) img_fill(L, M + bp, M + TB, ww - 2 * bp, 1, t->accent, 255); /* line under the title bar */
  img_disc(L, M + 16, M + TB / 2, CFG_DOT_R, CFG_DOT_CLOSE, 255, 0);
  img_disc(L, M + 36, M + TB / 2, CFG_DOT_R, CFG_DOT_MIN, 255, 0);
  img_disc(L, M + 56, M + TB / 2, CFG_DOT_R, CFG_DOT_MAX, 255, 0);
  int tw = text_width(fsans, CFG_TITLE_PX, title);
  draw_text(L, fsans, CFG_TITLE_PX, M + (ww - tw) / 2, M + TB / 2 + CFG_TITLE_PX * 2 / 5, title, t->accent ? t->accent : t->fg, CFG_TITLE_ALPHA);
}
/* the terminal cells with their colours and attributes, cursor as reverse video; (x0,y0) = top-left pixel of the grid */
static void chrome_grid(Img *L, const Theme *t, const Grid *gr, int x0, int y0) {
  for (int r = 0; r < gr->rows; r++) for (int c = 0; c < gr->cols; c++) {
    Cell k = gr->c[r * gr->cols + c]; uint8_t at = k.at;
    if (gr->cvis && r == gr->cy && c == gr->cx) at ^= 8;
    uint32_t fg = k.fg, bg = k.bg;
    if (at & 8) { uint32_t x = fg; fg = bg; bg = x; }
    int x = x0 + c * cw, y = y0 + r * ch;
    if (bg != t->bg) img_fill(L, x, y, cw, ch, bg, 255);
    if (k.cp > ' ') {
      int a = (at & 2) ? 140 : 255;
      draw_glyph(L, (at & 1) ? fmonob : fmono, k.cp, x, y + base, fg, a, 0);
    }
    if (at & 4) img_fill(L, x, y + base + 2, cw, 1, fg, 255);
  }
}
/* mouse pointer at (px,py); ripple 0 = none, 1.. = click animation step */
static void chrome_pointer(Img *F, double px, double py, int ripple) {
  static const char *arrow[] = {"X","XX","XWX","XWWX","XWWWX","XWWWWX","XWWWWWX","XWWWWWWX","XWWWWWWWX","XWWWWWWWWX","XWWWWWXXXXX","XWWXWWX","XWX XWWX","XX  XWWX","X    XWWX","     XWWX","      XX",0};
  int ox = (int)px, oy = (int)py;
  if (ripple) img_disc(F, ox, oy, 6 + ripple * 7, CFG_RIPPLE_COLOR, 200 - ripple * 40, 2.5);
  for (int j = 0; arrow[j]; j++) for (int i = 0; arrow[j][i]; i++) {
    char c = arrow[j][i]; if (c == ' ') continue;
    img_fill(F, ox + i * 2, oy + j * 2, 2, 2, c == 'X' ? CFG_POINTER_OUTLINE : CFG_POINTER_FILL, 255);
  }
}
/* caption bar, bottom (top = 0) or just under the title bar */
static void chrome_caption(Img *F, const char *text, int top, int M, int TB) {
  int px = CFG_CAPTION_PX, tw = text_width(fsansb, px, text), bw = tw + 36, bh = px + 22;
  if (bw > F->w - 20) bw = F->w - 20;
  int bx = (F->w - bw) / 2, by = top ? M + TB + 14 : F->h - M - bh - 14;
  img_rrect(F, bx, by, bw, bh, 10, CFG_CAPTION_BG, CFG_CAPTION_BG_ALPHA);
  draw_text(F, fsansb, px, bx + (bw - tw) / 2, by + bh / 2 + px / 3, text, CFG_CAPTION_FG, 255);
}
