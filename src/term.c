/* term.c - the terminal side: ANSI text -> Grid of cells (grid_parse) and the private tmux server (run_argv, tmux). */
/* ------------------------------------------------------------- grid ---- */
typedef struct { uint32_t cp, fg, bg; uint8_t at; } Cell; /* at: 1 bold 2 dim 4 underline 8 reverse */
typedef struct { int cols, rows, cx, cy, cvis; Cell *c; } Grid;

static uint32_t pal256(const Theme *t, int n) {
  if (n < 16) return t->pal[n];
  if (n < 232) { n -= 16; int r = n / 36, g = n / 6 % 6, b = n % 6; static const int l[] = {0,95,135,175,215,255}; return l[r] << 16 | l[g] << 8 | l[b]; }
  int v = 8 + (n - 232) * 10; return v << 16 | v << 8 | v;
}
static void grid_parse(Grid *g, const char *s, const Theme *t) {
  for (int i = 0; i < g->cols * g->rows; i++) g->c[i] = (Cell){' ', t->fg, t->bg, 0};
  int row = 0, col = 0, fgi = -1; uint32_t fg = t->fg, bg = t->bg; uint8_t at = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p && row < g->rows) {
    if (*p == '\n') { row++; col = 0; fg = t->fg; bg = t->bg; at = 0; fgi = -1; p++; continue; }
    if (*p == 27 && p[1] == '[') {
      p += 2; int v[16], nv = 0, cur = 0, have = 0;
      while (*p && !(*p >= 0x40 && *p <= 0x7e)) {
        if (*p == ';' || *p == ':') { if (nv < 16) v[nv++] = have ? cur : 0; cur = 0; have = 0; }
        else if (isdigit(*p)) { cur = cur * 10 + *p - '0'; have = 1; }
        p++;
      }
      if (nv < 16) v[nv++] = have ? cur : 0;
      if (*p == 'm') {
        for (int i = 0; i < nv; i++) {
          int k = v[i];
          if (k == 0) { fg = t->fg; bg = t->bg; at = 0; fgi = -1; }
          else if (k == 1) at |= 1; else if (k == 2) at |= 2; else if (k == 4) at |= 4; else if (k == 7) at |= 8;
          else if (k == 22) at &= ~3; else if (k == 24) at &= ~4; else if (k == 27) at &= ~8;
          else if (k >= 30 && k <= 37) { fg = t->pal[k - 30]; fgi = k - 30; }
          else if (k == 39) { fg = t->fg; fgi = -1; } else if (k == 49) bg = t->bg;
          else if (k >= 40 && k <= 47) bg = t->pal[k - 40];
          else if (k >= 90 && k <= 97) { fg = t->pal[k - 90 + 8]; fgi = -1; }
          else if (k >= 100 && k <= 107) bg = t->pal[k - 100 + 8];
          else if ((k == 38 || k == 48) && i + 1 < nv) {
            uint32_t c = 0; int ok = 0;
            if (v[i + 1] == 5 && i + 2 < nv) { c = pal256(t, v[i + 2] & 255); i += 2; ok = 1; }
            else if (v[i + 1] == 2 && i + 4 < nv) { c = (v[i + 2] & 255) << 16 | (v[i + 3] & 255) << 8 | (v[i + 4] & 255); i += 4; ok = 1; }
            if (ok) { if (k == 38) { fg = c; fgi = -1; } else bg = c; }
          }
        }
      }
      if (*p) p++;
      continue;
    }
    if (*p == 27) { p++; continue; }
    uint32_t cp = utf8_next(&p);
    if (col < g->cols) {
      uint32_t f = fg; if ((at & 1) && fgi >= 0) f = t->pal[fgi + 8];
      g->c[row * g->cols + col] = (Cell){cp, f, bg, at}; col++;
    }
  }
}

/* --------------------------------------------------------- tmux I/O ---- */
static char tm_sock[64], tm_conf[160], tm_home[128];
static int verbose;

/* run argv, optional capture of stdout (malloc'd); returns exit status */
static int run_argv(char *const *argv, char **out) {
  int pfd[2] = {-1, -1};
  if (out && pipe(pfd)) return -1;
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (!pid) {
    int dn = open("/dev/null", 2);
    dup2(dn, 0); dup2(out ? pfd[1] : dn, 1); if (!verbose) dup2(dn, 2);
    if (out) { close(pfd[0]); close(pfd[1]); }
    execvp(argv[0], argv); _exit(127);
  }
  if (out) {
    close(pfd[1]); size_t cap = 65536, n = 0; char *b = malloc(cap); ssize_t r;
    while ((r = read(pfd[0], b + n, cap - n - 1)) > 0) { n += r; if (n + 1 >= cap) b = realloc(b, cap *= 2); }
    b[n] = 0; close(pfd[0]); *out = b;
  }
  int st; waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static int tmux(char *out_[], char **out) { /* NULL-terminated argv after the common prefix */
  char *argv[64]; int n = 0;
  argv[n++] = "tmux"; argv[n++] = "-L"; argv[n++] = tm_sock; argv[n++] = "-f"; argv[n++] = tm_conf;
  for (int i = 0; out_[i] && n < 62; i++) argv[n++] = out_[i];
  argv[n] = 0;
  return run_argv(argv, out);
}
#define TMUX(...) tmux((char *[]){__VA_ARGS__, NULL}, NULL)
