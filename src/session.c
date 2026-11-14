/* session.c - start the program in the private tmux (or the gui backend). */
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
