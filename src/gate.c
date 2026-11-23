/* gate.c - quality gate: sample every captured screen, judge the demo at the end. Included after G exists. */
/* ---- quality gate: sample every captured screen, judge the demo at the end ---- */
static void gate_sample(G *g) {
  int total = g->gr.cols * g->gr.rows, cells = 0, ns = 0; uint64_t h = 1469598103934665603ULL;
  uint32_t seen[64][3]; 
  for (int i = 0; i < total; i++) {
    Cell *c = &g->gr.c[i];
    h = (h ^ c->cp) * 1099511628211ULL; h = (h ^ c->fg) * 1099511628211ULL; h = (h ^ c->bg) * 1099511628211ULL;
    if (c->cp && c->cp != ' ') {
      cells++; int f = 0;
      for (int k = 0; k < ns; k++) if (seen[k][0] == c->fg && seen[k][1] == c->bg && seen[k][2] == c->at) { f = 1; break; }
      if (!f && ns < 64) { seen[ns][0] = c->fg; seen[ns][1] = c->bg; seen[ns][2] = c->at; ns++; }
    }
  }
  if (cells > g->peak_cells) g->peak_cells = cells;
  if (ns > g->peak_styles) g->peak_styles = ns;
  if (h != g->last_hash) { g->nstates++; g->last_hash = h; }
}

/* returns number of errors; warnings only print. --force keeps the output anyway */
static int gate_verdict(G *g) {
  int total = g->cols * g->rows, err = 0;
  double fill = total ? 100.0 * g->peak_cells / total : 0;
  if (g->dead && g->dead_status != 0) {
    { say(LV_ERR, "the program died (exit status %d). Last screen:\n", g->dead_status); char *ds = g->dead_screen; size_t dl = strlen(ds); while (dl && isspace((unsigned char)ds[dl - 1])) ds[--dl] = 0; while (*ds == '\n') ds++; if (*ds) fprintf(stderr, "%s%s%s\n", color_on > 0 ? "\033[2m" : "", ds, color_on > 0 ? "\033[0m" : ""); } err++;
  }
  if (g->gui) { fill = 100; g->peak_styles = 99; }  /* pictures: no cell statistics */
  if (fill < CFG_GATE_EMPTY_PCT) { say(LV_ERR, "the screen is practically empty (peak %.1f%% of cells non-blank), nothing to show\n", fill); err++; }
  else if (fill < CFG_GATE_SPARSE_PCT) say(LV_WARN, "sparse screen (peak %.1f%% of cells non-blank); the demo may not show what the program looks like\n", fill);
  if (g->ninputs && g->nstates <= 1) { say(LV_ERR, "%d input(s) sent but the screen never changed\n", g->ninputs); err++; }
  else if (g->nstates <= 2 && g->T > 3000) say(LV_WARN, "only %d distinct screens in %.1fs; looks static\n", g->nstates, g->T / 1000.0);
  if (g->peak_styles < CFG_GATE_MIN_STYLES && fill >= 3.0) say(LV_WARN, "only %d text style(s) seen; no colour, probably not the real UI\n", g->peak_styles);
  err += leak_verdict(g);
  return err;
}

static int capture(G *g) {
  if (g->gui) return gui_capture(g);
  char *out = NULL;
  int rc = tmux((char *[]){"capture-pane", "-p", "-e", "-t", "d", ";", "display-message", "-p", "-t", "d", "@@CUR@@ #{cursor_x} #{cursor_y} #{cursor_flag} #{pane_dead} #{pane_dead_status}", NULL}, &out);
  if (rc || !out) { free(out); return -1; }
  char *m = strstr(out, "@@CUR@@"); int cx = 0, cy = 0, cf = 0, pd = 0, ps = 0;
  if (m) { sscanf(m + 7, "%d %d %d %d %d", &cx, &cy, &cf, &pd, &ps); *m = 0; }
  if (pd && !g->dead) { g->dead = 1; g->dead_status = ps; snprintf(g->dead_screen, sizeof g->dead_screen, "%s", out); }
  grid_parse(&g->gr, out, &THEMES[g->theme]); free(out);
  g->gr.cx = cx; g->gr.cy = cy; g->gr.cvis = cf;
  if (g->in_run && !g->dead) gate_sample(g);
  size_t sz = (size_t)g->gr.cols * g->gr.rows * sizeof(Cell);
  if (memcmp(g->gr.c, g->prev.c, sz) || g->gr.cx != g->prev.cx || g->gr.cy != g->prev.cy || g->gr.cvis != g->prev.cvis) {
    g->idle = 0;
    memcpy(g->prev.c, g->gr.c, sz); g->prev.cx = g->gr.cx; g->prev.cy = g->gr.cy; g->prev.cvis = g->gr.cvis;
    if (g->in_run && !g->dead) leak_grid(g);
    render_layer(g); g->dirty = 1;
  } else g->idle++;
  return 0;
}
/* one timeline step: let the app settle, sample the screen, show it for ms */
static void tick(G *g, int ms) {
  int settle = ms < 10 ? ms : 10;
  msleep(g, settle);
  if (g->have_session) capture(g);
  present_dirty(g, ms);
  msleep(g, ms - settle);
}
static void wait_ms(G *g, int ms) {
  int step = 1000 / g->fps;
  while (ms > 0) { int s = ms < step ? ms : step; tick(g, s); ms -= s; }
}
