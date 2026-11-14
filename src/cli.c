/* cli.c - usage text and the project-level commands (list / export / bulk / gen / sandbox). Included before main(). */
static int usage(void) {
  fprintf(stderr, "usage: autodemo render X.demo [-o OUT.(webp|mp4|webm)|FORMAT] [-q N] [-v] [--feature NAME] [--split] [--force] [--realtime] [--snap MS FILE.ppm]\n"
                  "       autodemo preview X.demo      (run and dump key screens as ASCII)\n"
                  "       autodemo check X.demo        (validate the script)\n"
                  "       autodemo export NAME[:FEATURE].fmt... [--split]   (NAME from .autodemo/examples; --split = one file per Feature)\n"
                  "       autodemo gen PROGRAM [-d DIR] [-s SETUP]... [-c CMD]...  (write a .demo that shows the program working: your -c commands, else man EXAMPLES, else README usage lines);  autodemo auto PROGRAM = gen + render\n"
                  "       autodemo features X.demo     (list the Feature sections)\n"
                  "       autodemo bulk export [fmt] [NAME...] | check | preview | clean | list\n"
                  "  no -o: writes NAME.<CFG_FORMAT> next to the .demo\n  -q N   webp: 0 = lossless, 1..100 = lossy; default CFG_QUALITY quality\n");
  return 2;
}

static const char *fmt_ext;

/* create the parent directories of a file path */
static void mkdir_p(const char *path) {
  char t[1024]; snprintf(t, sizeof t, "%s", path);
  for (char *p = t + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
}

/* ---- project-level commands: export / bulk / list (walk up to find .autodemo/examples) ---- */
static int find_root(char *root, size_t n) {
  char d[1024]; if (!getcwd(d, sizeof d)) return -1;
  for (;;) {
    char t[1100]; snprintf(t, sizeof t, "%s/.autodemo/examples", d);
    struct stat st; if (!stat(t, &st) && S_ISDIR(st.st_mode)) { snprintf(root, n, "%s", d); return 0; }
    char *sl = strrchr(d, '/'); if (!sl || sl == d) return -1; *sl = 0;
  }
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
/* sorted demo names (without .demo) */
static int list_demos(const char *root, char ***names) {
  char dir[1100]; snprintf(dir, sizeof dir, "%s/.autodemo/examples", root);
  DIR *d = opendir(dir); if (!d) return 0;
  char **v = NULL; int n = 0; struct dirent *e;
  while ((e = readdir(d))) {
    size_t l = strlen(e->d_name); if (l < 6 || strcmp(e->d_name + l - 5, ".demo")) continue;
    v = realloc(v, (n + 1) * sizeof *v); v[n] = strndup(e->d_name, l - 5); n++;
  }
  closedir(d); if (n) qsort(v, n, sizeof *v, cmp_str);
  *names = v; return n;
}
static int run_self(const char *self, char *const *args) {
  pid_t p = fork(); if (p < 0) return -1;
  if (!p) { execv(self, args); _exit(127); }
  int st; waitpid(p, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
/* one demo through self: op = render|check|preview; fmt used for render */
static int one(const char *self, const char *root, const char *op, const char *name, const char *fmt, const char *feature, int split) {
  char in[1200], out[1200];
  snprintf(in, sizeof in, "%s/.autodemo/examples/%s.demo", root, name);
  struct stat st; if (stat(in, &st)) { say(LV_ERR, "no demo '%s' (%s)\n", name, in); return 1; }
  if (!strcmp(op, "render")) {
    char *a[10]; int n = 0; a[n++] = (char *)self; a[n++] = "render"; a[n++] = in;
    if (feature) { char sl[128]; feat_slug(feature, sl, sizeof sl); snprintf(out, sizeof out, "%s/.autodemo/examples/%s.%s.%s", root, name, sl, fmt); a[n++] = "--feature"; a[n++] = (char *)feature; }
    else snprintf(out, sizeof out, "%s/.autodemo/examples/%s.%s", root, name, fmt);
    a[n++] = "-o"; a[n++] = out; if (split) a[n++] = "--split"; a[n] = NULL;
    return run_self(self, a);
  }
  return run_self(self, (char *[]){(char *)self, (char *)op, in, NULL});
}
static int is_fmt(const char *f) { return !strcmp(f, "webp") || !strcmp(f, "mp4") || !strcmp(f, "webm"); }

#include "sandbox.c"
#include "gen.c"
static int project_cmd(const char *self, int argc, char **argv) {
  const char *m = argv[1];
  char root[1024];
  if (find_root(root, sizeof root)) { say(LV_ERR, "no .autodemo/examples found here or above\n"); return 1; }
  int split = 0;
  for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--split")) { split = 1; memmove(argv + i, argv + i + 1, (size_t)(argc - i) * sizeof *argv); argc--; i--; }
  char **all; int na = list_demos(root, &all);
  if (!strcmp(m, "list")) { for (int i = 0; i < na; i++) puts(all[i]); return 0; }
  if (!strcmp(m, "export")) { /* export NAME.fmt ... ; NAME alone = webp */
    if (argc < 3) return usage();
    int bad = 0;
    for (int i = 2; i < argc; i++) {
      char nm[256]; snprintf(nm, sizeof nm, "%s", argv[i]); const char *fmt = CFG_FORMAT, *feat = NULL;
      char *dot = strrchr(nm, '.');
      if (dot && is_fmt(dot + 1)) { fmt = dot + 1; *dot = 0; }
      else if (dot && !strcmp(dot, ".demo")) *dot = 0;
      char *colon = strchr(nm, ':'); if (colon) { *colon = 0; feat = colon + 1; } /* NAME:FEATURE.fmt */
      bad |= one(self, root, "render", nm, fmt, feat, split) != 0;
    }
    return bad;
  }
  /* bulk OP [FMT] [NAME...] : OP = export|check|preview|clean|list ; no names = every demo */
  if (argc < 3) { fprintf(stderr, "usage: autodemo bulk export|check|preview|clean|list [webp|mp4|webm] [NAME...] [--split]\n"); return 2; }
  const char *op = argv[2], *fmt = CFG_FORMAT; int i = 3;
  if (!strcmp(op, "export") && i < argc && is_fmt(argv[i])) fmt = argv[i++];
  char **names = argv + i; int nn = argc - i;
  if (nn == 0) { names = all; nn = na; }
  if (!strcmp(op, "list")) { for (int k = 0; k < nn; k++) puts(names[k]); return 0; }
  int bad = 0, ok = 0, skipped = 0;
  for (int k = 0; k < nn; k++) {
    if (!strcmp(op, "clean")) {
      for (const char **f = (const char *[]){"webp", "mp4", "webm", NULL}; *f; f++) {
        char p[1300]; snprintf(p, sizeof p, "%s/.autodemo/examples/%s.%s", root, names[k], *f);
        if (!unlink(p)) printf("removed %s\n", p);
      }
      continue;
    }
    const char *rop = !strcmp(op, "export") ? "render" : op;
    if (strcmp(rop, "render") && strcmp(rop, "check") && strcmp(rop, "preview")) { say(LV_ERR, "unknown bulk op '%s'\n", op); return 2; }
    printf("== %s %s\n", op, names[k]); fflush(stdout);
    int rc = one(self, root, rop, names[k], fmt, NULL, split);
    if (rc == 0) ok++; else if (rc == 3) { skipped++; } else bad++;
  }
  if (strcmp(op, "clean")) printf("bulk %s: %d ok, %d failed, %d skipped\n", op, ok, bad, skipped);
  return bad != 0;
}

static double cli_speed;
