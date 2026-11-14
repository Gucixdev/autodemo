/* draw.c - RGBA picture and the drawing primitives (fill, disc, rounded rectangle, soft glow). No state, no I/O. */
/* ------------------------------------------------------------ images ---- */
typedef struct { int w, h; uint8_t *p; } Img; /* RGBA */
static Img img_new(int w, int h) { Img i = {w, h, calloc((size_t)w * h, 4)}; return i; }
static void img_fill(Img *im, int x, int y, int w, int h, uint32_t c, int a) {
  for (int j = y < 0 ? 0 : y; j < y + h && j < im->h; j++)
    for (int i = x < 0 ? 0 : x; i < x + w && i < im->w; i++) {
      uint8_t *p = im->p + ((size_t)j * im->w + i) * 4;
      p[0] = (p[0] * (255 - a) + (c >> 16 & 255) * a) / 255;
      p[1] = (p[1] * (255 - a) + (c >> 8 & 255) * a) / 255;
      p[2] = (p[2] * (255 - a) + (c & 255) * a) / 255; p[3] = 255;
    }
}
static void img_px(Img *im, int x, int y, uint32_t c, int a) { if (a > 0) img_fill(im, x, y, 1, 1, c, a > 255 ? 255 : a); }
static void img_disc(Img *im, double cx, double cy, double r, uint32_t c, int a, double ring) {
  for (int j = (int)(cy - r - 1); j <= cy + r + 1; j++) for (int i = (int)(cx - r - 1); i <= cx + r + 1; i++) {
    double d = hypot(i - cx, j - cy); if (d > r) continue;
    if (ring > 0 && d < r - ring) continue;
    img_px(im, i, j, c, a);
  }
}
static void img_rrect(Img *im, int x, int y, int w, int h, int r, uint32_t c, int a) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    int dx = i < r ? r - i : i >= w - r ? i - (w - r - 1) : 0, dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
    if (dx * dx + dy * dy > r * r) continue;
    img_px(im, x + i, y + j, c, a);
  }
}
/* soft radial light: alpha falls off quadratically from the centre */
static void img_glow(Img *im, int cx, int cy, int r, uint32_t c, int maxa) {
  for (int y = cy - r < 0 ? 0 : cy - r; y < im->h && y <= cy + r; y++)
    for (int x = cx - r < 0 ? 0 : cx - r; x < im->w && x <= cx + r; x++) {
      double d = sqrt((double)((x - cx) * (x - cx) + (y - cy) * (y - cy))) / r; if (d >= 1) continue;
      img_px(im, x, y, c, (int)(maxa * (1 - d) * (1 - d)));
    }
}
