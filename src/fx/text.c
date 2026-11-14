/* text.c - FreeType fonts, UTF-8 and text drawing onto an Img. Holds the font faces and the cell size (cw, ch, base). */
/* ------------------------------------------------------------- fonts ---- */
static FT_Library ft;
static FT_Face fmono, fmonob, fsans, fsansb;
static int mono_px, cw, ch, base;

static FT_Face load_face(const char *const *paths) {
  FT_Face f;
  for (; *paths; paths++) if (!FT_New_Face(ft, *paths, 0, &f)) return f;
  return NULL;
}
/* "~/x" -> $HOME/x (read before the private HOME is set) */
static FT_Face load_face_tilde(const char *const *paths) {
  for (; *paths; paths++) {
    char b[1024];
    if (paths[0][0] == '~' && paths[0][1] == '/' && getenv("HOME")) snprintf(b, sizeof b, "%s%s", getenv("HOME"), *paths + 1); else snprintf(b, sizeof b, "%s", *paths);
    FT_Face f; if (!FT_New_Face(ft, b, 0, &f)) return f;
  }
  return NULL;
}
static void face_px(FT_Face f, int px) { if (FT_IS_SCALABLE(f)) FT_Set_Pixel_Sizes(f, 0, px); else FT_Select_Size(f, 0); }

static int fonts_init(int px, const Theme *th) {
  static const char *m[] = CFG_FONT_MONO, *mb[] = CFG_FONT_MONO_BOLD, *s[] = CFG_FONT_SANS, *sb[] = CFG_FONT_SANS_BOLD;
  if (FT_Init_FreeType(&ft)) return -1;
  const char *ov = getenv("AUTODEMO_FONT");
  const char *ovl[] = {ov, 0};
  fmono = ov ? load_face(ovl) : NULL;
  if (!fmono && th->font) { fmono = load_face_tilde(th->font); if (fmono && th->font_bold) fmonob = load_face_tilde(th->font_bold); if (fmono && th->font_px) px = th->font_px; }
  if (!fmono) fmono = load_face(m);
  if (!fmonob) fmonob = load_face(mb); fsans = load_face(s); fsansb = load_face(sb);
  if (!fmono) return -1;
  if (!fmonob) fmonob = fmono;
  if (!fsans) fsans = fmono;
  if (!fsansb) fsansb = fsans;
  mono_px = px;
  face_px(fmono, px); face_px(fmonob, px);
  if (FT_Load_Char(fmono, 'M', FT_LOAD_DEFAULT)) return -1;
  cw = (int)((fmono->glyph->advance.x + 32) >> 6);
  int asc = (int)((fmono->size->metrics.ascender + 63) >> 6), desc = (int)(-fmono->size->metrics.descender + 63) >> 6;
  ch = asc + desc; base = asc;
  return 0;
}
static uint32_t utf8_next(const unsigned char **sp) {
  const unsigned char *s = *sp; uint32_t c = *s++;
  int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
  if (extra) c &= 0x3f >> extra;
  while (extra-- && (*s & 0xc0) == 0x80) c = (c << 6) | (*s++ & 0x3f);
  *sp = s; return c;
}
static void put_utf8(FILE *f, uint32_t c) {
  if (c < 0x80) fputc(c, f);
  else if (c < 0x800) { fputc(0xc0 | c >> 6, f); fputc(0x80 | (c & 63), f); }
  else if (c < 0x10000) { fputc(0xe0 | c >> 12, f); fputc(0x80 | (c >> 6 & 63), f); fputc(0x80 | (c & 63), f); }
  else { fputc(0xf0 | c >> 18, f); fputc(0x80 | (c >> 12 & 63), f); fputc(0x80 | (c >> 6 & 63), f); fputc(0x80 | (c & 63), f); }
}
/* draw one glyph with origin (x, baseline); returns advance */
static int draw_glyph(Img *im, FT_Face f, uint32_t cp, int x, int bl, uint32_t col, int alpha, int embolden) {
  if (FT_Load_Char(f, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return 0;
  FT_GlyphSlot g = f->glyph; FT_Bitmap *b = &g->bitmap;
  for (unsigned r = 0; r < b->rows; r++) for (unsigned c = 0; c < b->width; c++) {
    int a = b->pixel_mode == FT_PIXEL_MODE_MONO ? ((b->buffer[r * b->pitch + (c >> 3)] >> (7 - (c & 7))) & 1) * alpha
                                                : b->buffer[r * b->pitch + c] * alpha / 255;
    img_px(im, x + g->bitmap_left + (int)c, bl - g->bitmap_top + (int)r, col, a);
    if (embolden) img_px(im, x + g->bitmap_left + (int)c + 1, bl - g->bitmap_top + (int)r, col, a);
  }
  return (int)((g->advance.x + 32) >> 6);
}
static int text_width(FT_Face f, int px, const char *s) {
  int w = 0; FT_Set_Pixel_Sizes(f, 0, px);
  for (const unsigned char *p = (const unsigned char *)s; *p;) { uint32_t c = utf8_next(&p); if (!FT_Load_Char(f, c, FT_LOAD_DEFAULT)) w += (int)((f->glyph->advance.x + 32) >> 6); }
  return w;
}
static int draw_text(Img *im, FT_Face f, int px, int x, int bl, const char *s, uint32_t col, int alpha) {
  FT_Set_Pixel_Sizes(f, 0, px); int x0 = x;
  for (const unsigned char *p = (const unsigned char *)s; *p;) x += draw_glyph(im, f, utf8_next(&p), x, bl, col, alpha, 0);
  return x - x0;
}
