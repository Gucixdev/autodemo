/* gen: write a .demo from a program (no script needed) that shows it WORKING, never its --help (that is the README's job).
 * Commands come from, in this order: your -c commands (trusted as given), the EXAMPLES section of its man page, usage lines
 * in a README next to the program. No commands found = no demo. The file is plain text, edit it freely.
 * Commands from man pages / READMEs pass a deny list and a shape filter; they run in the private HOME of the render, not in the sandbox. */
/* man examples are written for a real system: keep only self-contained, relative, local commands (no absolute paths, hosts, URLs, variables, continuation lines) */
static int gen_unsafe_shape(const char *c) {
  size_t n = strlen(c); if (n && c[n - 1] == '\\') return 1;
  for (const char *t = c; *t; t++) {
    int start = t == c || t[-1] == ' ';
    if (start && (*t == '/' || (*t == '-' && t[1] != '-' && 0))) return 1;
    if (*t == '$' || *t == '{' || *t == '`' || *t == '\\' ) { if (!(*t == '\\' && t[1] == ';')) return 1; }
    if (*t == ':' && t[1] != ' ' && t[1] && t > c) return 1;
  }
  return 0;
}
static int gen_denied(const char *c) {
  static const char *bad[] = {"sudo", "rm ", " rm", "dd ", "mkfs", "shred", "chmod -R", "chown -R", ">", "mv /", ":(){", "reboot", "shutdown", "kill", "wipefs", "fdisk", "-delete", "--remove", "-exec", NULL};
  for (int i = 0; bad[i]; i++) if (strstr(c, bad[i])) return 1;
  return 0;
}
static void gen_esc(FILE *f, const char *s) { for (; *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); fputc(*s, f); } }
static int gen_resolve(const char *prog, char *out, size_t n, int *on_path) {
  *on_path = 0;
  if (strchr(prog, '/')) { if (!realpath(prog, out) || access(out, X_OK)) return -1; return 0; }
  const char *path = getenv("PATH"); if (!path) return -1;
  char *p = strdup(path), *sv = NULL;
  for (char *d = strtok_r(p, ":", &sv); d; d = strtok_r(NULL, ":", &sv)) { snprintf(out, n, "%s/%s", d, prog); if (!access(out, X_OK)) { free(p); *on_path = 1; return 0; } }
  free(p); return -1;
}
static int gen_cmd(const char *self, const char *prog, int render, const char *outdir, char **cust, int ncust, char **setup, int nsetup) {
  char abs_[1024]; int on_path; if (gen_resolve(prog, abs_, sizeof abs_, &on_path)) { say(LV_ERR, "gen: cannot find executable '%s'\n", prog); return 1; }
  const char *name = strrchr(abs_, '/'); name = name ? name + 1 : abs_;
  char cmd[2048], line[1024];
  FILE *p;
  char *ex[8]; int nex = 0;
  for (int i = 0; i < ncust && i < 8; i++) ex[nex++] = strdup(cust[i]);   /* -c: your own commands, trusted as given */
  snprintf(cmd, sizeof cmd, "MANWIDTH=120 man -P cat '%s' 2>/dev/null | col -bx", name);
  if (!ncust && (p = popen(cmd, "r"))) {
    int in = 0;
    while (nex < 3 && fgets(line, sizeof line, p)) {
      line[strcspn(line, "\n")] = 0;
      if (!strncmp(line, "EXAMPLE", 7)) { in = 1; continue; }
      if (in && line[0] && !isspace((unsigned char)line[0])) in = 0;               /* next section heading */
      if (!in) continue;
      char *t = line; while (isspace((unsigned char)*t)) t++;
      if (*t == '$') { t++; while (isspace((unsigned char)*t)) t++; }
      size_t nl = strlen(name);
      if (!strncmp(t, name, nl) && t[nl] == ' ' && strlen(t) < 90 && !gen_denied(t) && !gen_unsafe_shape(t)) ex[nex++] = strdup(t);
    }
    pclose(p);
  }
  if (!ncust && nex < 4) { /* README.md next to the program or up to 3 levels above: lines "$ NAME ..." or "NAME ..." inside code fences */
    char d[1024]; snprintf(d, sizeof d, "%s", abs_);
    for (int up = 0; up < 4 && nex < 4; up++) {
      char *sl = strrchr(d, '/'); if (!sl || sl == d) break; *sl = 0;
      char rp[1100]; snprintf(rp, sizeof rp, "%s/README.md", d); FILE *r = fopen(rp, "r"); if (!r) continue;
      int fence = 0; size_t nl = strlen(name);
      while (nex < 4 && fgets(line, sizeof line, r)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "```", 3)) { fence = !fence; continue; }
        if (!fence) continue;
        char *t = line; while (isspace((unsigned char)*t)) t++;
        if (*t == '$') { t++; while (isspace((unsigned char)*t)) t++; }
        if (strncmp(t, name, nl) || (t[nl] != ' ' && t[nl] != 0)) continue;
        if (strlen(t) < 90 && strlen(t) > nl && !gen_denied(t) && !gen_unsafe_shape(t) && !strstr(t, "--help") && !strstr(t, " -h")) {
          int dup = 0; for (int i = 0; i < nex; i++) if (!strcmp(ex[i], t)) dup = 1;
          if (!dup) ex[nex++] = strdup(t);
        }
      }
      fclose(r); if (nex) break;
    }
  }
  if (!nex) { say(LV_ERR, "gen: no usage examples for '%s' (no EXAMPLES in its man page, none in a nearby README.md); pass what to show: autodemo gen %s -c 'cmd' -c 'cmd'\n", name, prog); return 1; }
  char root[1024]; if (find_root(root, sizeof root)) { if (getcwd(root, sizeof root) == NULL) return 1; }
  char path[1400]; if (outdir) snprintf(path, sizeof path, "%s/%s.demo", outdir, name); else snprintf(path, sizeof path, "%s/.autodemo/examples/%s.demo", root, name); mkdir_p(path);
  { FILE *o = fopen(path, "r"); if (o) { char h[40] = {0}; if (!fgets(h, sizeof h, o)) h[0] = 0; fclose(o);
      if (strncmp(h, "# generated by", 14)) { say(LV_ERR, "gen: %s exists and was not generated; not overwriting (delete or rename it)\n", path); return 1; } } }
  FILE *f = fopen(path, "w"); if (!f) { perror(path); return 1; }
  fprintf(f, "# generated by: autodemo gen %s  (plain text, edit freely)\nSize 100x28\nTheme dracula\nTitle \"%s\"\n", prog, name);
  fprintf(f, "Setup printf \"PS1='\\\\$ '\\n\" > $HOME/.bashrc\n");
  if (!on_path) { char dir[1024]; snprintf(dir, sizeof dir, "%s", abs_); *strrchr(dir, '/') = 0;
    if (strchr(dir, '\'')) { say(LV_ERR, "gen: quote in path %s\n", dir); fclose(f); return 1; }
    fprintf(f, "Setup printf 'export PATH=%%s:$PATH\\n' '%s' >> $HOME/.bashrc\n", dir); }
  fprintf(f, "Setup mkdir -p $HOME/proj/src && printf 'hello\\n' > $HOME/proj/README.md && touch $HOME/proj/Makefile $HOME/proj/src/main.c\n");
  for (int i = 0; i < nsetup; i++) fprintf(f, "Setup %s\n", setup[i]); /* -s: files the commands need, made before the recording starts */
  fprintf(f, "Cd /tmp\nRun bash -i\nSend \"cd ~/proj\\n\"\nSleep 400ms\nCaption \"%s\" bottom\n", name);
  for (int i = 0; i < nex; i++) { fprintf(f, "Caption \"%s %d\" bottom\nSend \"", ncust ? "Step" : "Example", i + 1); gen_esc(f, ex[i]); fprintf(f, "\" 35\nSendKey Enter\nSleep 1500ms\n"); free(ex[i]); }
  fprintf(f, "Loop\n"); fclose(f);
  say(LV_OK, "wrote %s (%d command%s)\n", path, nex, nex == 1 ? "" : "s");
  if (!render || outdir) { if (!outdir) printf("next: autodemo export %s\n", name); return 0; }
  int rc = one(self, root, "render", name, CFG_FORMAT, NULL, 0);
  if (!rc) printf("README: ![%s demo](.autodemo/examples/%s.%s)\n", name, name, CFG_FORMAT);
  return rc != 0;
}
