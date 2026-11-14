/* input.c - what a script does to the program: typing, keys, scrolling, mouse. Included after the recorder and the transitions. */
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
