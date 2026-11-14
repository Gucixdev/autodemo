/* fx.c - transition effects between two RGBA pictures of the same size. Pure functions on pixel buffers: no state, no
 * allocation, nothing from autodemo.c. fx_blend() writes the picture at progress u (0 = all A, 1 = all B, already eased).
 *
 *   fade                 A dissolves into B
 *   slide  slide-right   A leaves to the left (right), B enters from the right (left)
 *   slide-up slide-down  A leaves upward (downward), B enters from below (above)
 *   zoom                 A zooms in while it fades out, B grows into place while it fades in
 *   zoom-out             A shrinks away, B comes from larger down to size
 *   wipe                 B is revealed from the left with a soft edge
 *   none                 B at once
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "config.h"

enum { FX_NONE, FX_FADE, FX_SLIDE_L, FX_SLIDE_R, FX_SLIDE_U, FX_SLIDE_D, FX_ZOOM, FX_ZOOM_OUT, FX_WIPE };

static const struct { const char *name; int id; } FX_NAMES[] = {
  {"none", FX_NONE}, {"fade", FX_FADE}, {"slide", FX_SLIDE_L}, {"slide-left", FX_SLIDE_L}, {"slide-right", FX_SLIDE_R},
  {"slide-up", FX_SLIDE_U}, {"slide-down", FX_SLIDE_D}, {"zoom", FX_ZOOM}, {"zoom-out", FX_ZOOM_OUT}, {"wipe", FX_WIPE}, {0, 0}};

/* effect id for a name, -1 if there is no such effect */
static int fx_parse(const char *name) {
  for (int i = 0; FX_NAMES[i].name; i++) if (!strcmp(FX_NAMES[i].name, name)) return FX_NAMES[i].id;
  return -1;
}

static const char *fx_list(void) { return "none|fade|slide|slide-left|slide-right|slide-up|slide-down|zoom|zoom-out|wipe"; }

static inline uint8_t fx_mix(uint8_t a, uint8_t b, int k256) { return (uint8_t)((a * (256 - k256) + b * k256) >> 8); }

/* the pixel of src at (x,y) after scaling by s around the centre, edges clamped */
static inline const uint8_t *fx_sample(const uint8_t *src, int w, int h, int x, int y, double s) {
  int sx = (int)((x - w * 0.5) / s + w * 0.5), sy = (int)((y - h * 0.5) / s + h * 0.5);
  if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
  if (sy < 0) sy = 0; else if (sy >= h) sy = h - 1;
  return src + ((size_t)sy * w + sx) * 4;
}

static void fx_blend(uint8_t *out, const uint8_t *a, const uint8_t *b, int w, int h, int fx, double u) {
  size_t row = (size_t)w * 4, total = row * h;
  int k = (int)(u * 256);
  if (u < 0) u = 0; else if (u > 1) u = 1;
  switch (fx) {
  case FX_FADE:
    for (size_t i = 0; i < total; i++) out[i] = fx_mix(a[i], b[i], k);
    break;
  case FX_SLIDE_L: case FX_SLIDE_R: {
    int keep = (int)((1 - u) * w); /* columns of A still visible */
    for (int y = 0; y < h; y++) {
      uint8_t *o = out + y * row; const uint8_t *ar = a + y * row, *br = b + y * row;
      if (fx == FX_SLIDE_L) { memcpy(o, ar + (size_t)(w - keep) * 4, (size_t)keep * 4); memcpy(o + (size_t)keep * 4, br, (size_t)(w - keep) * 4); }
      else { memcpy(o, br + (size_t)keep * 4, (size_t)(w - keep) * 4); memcpy(o + (size_t)(w - keep) * 4, ar, (size_t)keep * 4); }
    }
    break; }
  case FX_SLIDE_U: case FX_SLIDE_D: {
    int keep = (int)((1 - u) * h); /* rows of A still visible */
    if (fx == FX_SLIDE_U) { memcpy(out, a + (size_t)(h - keep) * row, (size_t)keep * row); memcpy(out + (size_t)keep * row, b, (size_t)(h - keep) * row); }
    else { memcpy(out, b + (size_t)keep * row, (size_t)(h - keep) * row); memcpy(out + (size_t)(h - keep) * row, a, (size_t)keep * row); }
    break; }
  case FX_ZOOM: case FX_ZOOM_OUT: {
    double z = CFG_ZOOM_AMOUNT;
    double sa = fx == FX_ZOOM ? 1 + z * u : 1 - z * u;         /* A: in, or shrinking away */
    double sb = fx == FX_ZOOM ? 1 - z + z * u : 1 + z - z * u; /* B: grows into place, or comes down to size */
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const uint8_t *pa = fx_sample(a, w, h, x, y, sa), *pb = fx_sample(b, w, h, x, y, sb);
        uint8_t *o = out + y * row + (size_t)x * 4;
        for (int c = 0; c < 4; c++) o[c] = fx_mix(pa[c], pb[c], k);
      }
    break; }
  case FX_WIPE: {
    double soft = CFG_WIPE_SOFT * w, edge = u * (w + soft); /* the edge runs from -soft to w */
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        double t = (edge - x) / (soft > 0 ? soft : 1); /* 0 before the edge, 1 behind it */
        int kk = t <= 0 ? 0 : t >= 1 ? 256 : (int)(t * 256);
        size_t i = (size_t)y * row + (size_t)x * 4;
        for (int c = 0; c < 4; c++) out[i + c] = fx_mix(a[i + c], b[i + c], kk);
      }
    break; }
  default: /* FX_NONE */
    memcpy(out, b, total);
  }
}
