/* exec.c - header directives, duration estimate and execute(): run the parsed script against the recorder. */
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
