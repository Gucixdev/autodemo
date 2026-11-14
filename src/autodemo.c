/* autodemo - scripted, non-interactive demo recorder -> animated WebP / mp4 / webm.
 * See README.md. Build: make. TUI backend = private tmux server + software renderer. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <limits.h>
#include <strings.h>
#include <dirent.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <webp/encode.h>
#include <webp/mux.h>
#include "fx/transition.c"
#include "fx/theme.c"


/* ------------------------------------------------------------ output ---- */
/* one place for messages: red error, yellow warning, green ok, dim note; plain text when not a terminal (or NO_COLOR) */
enum { LV_ERR, LV_WARN, LV_OK, LV_NOTE };
static int color_on = -1;
static void say(int lv, const char *fmt, ...) {
  if (color_on < 0) color_on = (isatty(2) || getenv("AUTODEMO_COLOR")) && !getenv("NO_COLOR");
  static const char *col[] = {"\033[1;31m", "\033[1;33m", "\033[32m", "\033[2m"};
  static const char *sym[] = {"error", "warn ", "ok   ", "note "};
  char m[2048]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
  size_t l = strlen(m); while (l && m[l - 1] == '\n') m[--l] = 0;
  if (color_on) fprintf(stderr, "%s%s\033[0m %s%s\033[0m\n", col[lv], sym[lv], lv == LV_ERR ? "\033[31m" : lv == LV_WARN ? "\033[33m" : "", m);
  else fprintf(stderr, "%s %s\n", sym[lv], m);
}

/* ------------------------------------------------------------ script ---- */
enum { C_RUN, C_TYPE, C_KEY, C_WAIT, C_CAPTION, C_SLIDE, C_TRANS, C_CMOVE, C_CCLICK,
       C_CHIDE, C_CSHOW, C_WINDOW, C_THEME, C_SIZE, C_LOOP, C_FPS, C_FONT, C_TITLE,
       C_ENV, C_CD, C_SETUP, C_SPEED, C_AUTOCUT, C_TYPING, C_CUT, C_CUTEND, C_WAITFOR, C_FEATURE, C_FEATEND, C_PSCROLL, C_MSCROLL, C_NOP };
typedef struct { int op, line, n; char *a[6]; } Cmd;
typedef struct { Cmd *v; int n, cap; } Script;

static const struct { const char *name; int op, min, max; } SPEC[] = {
  {"run",C_RUN,1,1},{"send",C_TYPE,1,2},{"sendkey",C_KEY,1,5},{"sleep",C_WAIT,1,1},{"mousemove",C_CMOVE,2,3},{"mouseclick",C_CCLICK,0,0},{"mousehide",C_CHIDE,0,0},{"mouseshow",C_CSHOW,0,0},
  {"caption",C_CAPTION,1,3},{"slide",C_SLIDE,1,3},{"transition",C_TRANS,1,3},{"speed",C_SPEED,1,1},{"autocut",C_AUTOCUT,1,1},{"typing",C_TYPING,1,1},{"cut",C_CUT,0,0},{"cutend",C_CUTEND,0,0},{"waitfor",C_WAITFOR,1,2},{"feature",C_FEATURE,1,1},{"pagescroll",C_PSCROLL,1,2},{"mousescroll",C_MSCROLL,1,2},
  {"window",C_WINDOW,1,1},{"theme",C_THEME,1,1},{"size",C_SIZE,1,1},{"loop",C_LOOP,0,0},
  {"fps",C_FPS,1,1},{"font",C_FONT,1,1},{"title",C_TITLE,1,1},{"env",C_ENV,1,1},
  {"cd",C_CD,1,1},{"setup",C_SETUP,1,1},{0}};

static int dur_ms(const char *s, int *out) {
  char *e; double v = strtod(s, &e);
  if (e == s || v < 0) return -1;
  if (!*e || !strcmp(e, "ms")) *out = (int)v;
  else if (!strcmp(e, "s")) *out = (int)(v * 1000);
  else return -1;
  return 0;
}

/* split one line into tokens (quotes, \n \" \\ \t escapes, # comments) */
static int tokenize(char *s, char **tok, int max, char *err) {
  int n = 0;
  while (*s) {
    while (isspace((unsigned char)*s)) s++;
    if (!*s || *s == '#') break;
    if (n >= max) { strcpy(err, "too many arguments"); return -1; }
    char *w = s;
    tok[n++] = w;
    if (*s == '"') {
      s++;
      for (;;) {
        if (!*s) { strcpy(err, "unterminated string"); return -1; }
        if (*s == '"') { s++; break; }
        if (*s == '\\' && s[1]) {
          s++; *w++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'e' ? 27 : *s; s++;
        } else *w++ = *s++;
      }
      *w = 0;
    } else {
      while (*s && !isspace((unsigned char)*s)) s++;
      if (*s) *s++ = 0;
    }
  }
  return n;
}

static int valid_key(const char *k) {
  static const char *names[] = {"Enter","Escape","Tab","BTab","Space","BSpace","Up","Down","Left","Right",
    "Home","End","PageUp","PageDown","PgUp","PgDn","DC","IC","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12",0};
  char b[64]; strncpy(b, k, 63); b[63] = 0;
  char *star = strchr(b, '*'); if (star) { *star = 0; if (atoi(star + 1) < 1) return 0; }
  for (int i = 0; names[i]; i++) if (!strcmp(b, names[i])) return 1;
  if (strlen(b) == 1) return 1;
  if ((b[0] == 'C' || b[0] == 'M' || b[0] == 'S') && b[1] == '-' && b[2]) return 1;
  return 0;
}

/* command names are case-insensitive: Send, send and SEND are the same */
static void alias_cmd(char **tok, int *n) {
  static char low[32]; size_t l = strlen(tok[0]); (void)n;
  if (l >= sizeof low) return;
  for (size_t i = 0; i <= l; i++) low[i] = (char)tolower((unsigned char)tok[0][i]);
  tok[0] = low;
}

/* ---- readability layer: statements may end with ';', several may share a line, a command may be written
   Send("text"), and Feature "x" { ... } / Cut { ... } blocks close with '}'. All optional; plain lines still work.
   Run/Setup/Cd/Env keep the rest of the line verbatim (shell code has its own ';'). ---- */
typedef struct { char *t; int line; } Stmt;
static void stmt_add(Stmt **v, size_t *n, size_t *cap, const char *t, size_t len, int line) {
  while (len && isspace((unsigned char)*t)) { t++; len--; }
  while (len && isspace((unsigned char)t[len - 1])) len--;
  if (!len) return;
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 64; *v = realloc(*v, *cap * sizeof **v); }
  (*v)[*n].t = strndup(t, len); (*v)[*n].line = line; (*n)++;
}
/* Name(a, b) -> Name a b  (only when '(' directly follows the command name; commas outside quotes become spaces) */
static void call_form(char *t) {
  size_t i = 0; while (isalnum((unsigned char)t[i])) i++;
  if (!i || t[i] != '(') return;
  t[i] = ' '; int q = 0, last = -1;
  for (size_t k = i + 1; t[k]; k++) {
    if (t[k] == '\\' && q && t[k + 1]) { k++; continue; }
    if (t[k] == '"') q = !q;
    else if (!q && t[k] == ',') t[k] = ' ';
    else if (!q && t[k] == ')') last = (int)k;
  }
  if (last >= 0) t[last] = ' ';
}
static void expand_line(const char *buf, int line, Stmt **v, size_t *n, size_t *cap) {
  const char *w = buf; while (isspace((unsigned char)*w)) w++;
  size_t wl = strcspn(w, " \t\n(");
  if ((wl == 3 && !strncasecmp(w, "run", 3)) || (wl == 5 && !strncasecmp(w, "setup", 5)) || (wl == 2 && !strncasecmp(w, "cd", 2)) || (wl == 3 && !strncasecmp(w, "env", 3))) {
    stmt_add(v, n, cap, buf, strlen(buf), line); return; }
  const char *st = buf; int q = 0; size_t i = 0;
  for (; buf[i]; i++) {
    char c = buf[i];
    if (q) { if (c == '\\' && buf[i + 1]) i++; else if (c == '"') q = 0; continue; }
    if (c == '"') { q = 1; continue; }
    if (c == '#') break;
    if (c == ';' || c == '{' || c == '}') {
      stmt_add(v, n, cap, st, (size_t)(buf + i - st), line);
      if (c != ';') { char m[3] = {1, c, 0}; stmt_add(v, n, cap, m, 2, line); }
      st = buf + i + 1;
    }
  }
  stmt_add(v, n, cap, st, (size_t)(buf + i - st), line);
}

/* returns 0 ok; on error prints file:line and returns -1 (continues to report all) */
static int parse_script(const char *path, Script *sc) {
  FILE *f = fopen(path, "r");
  if (!f) { say(LV_ERR, "%s: %s\n", path, strerror(errno)); return -1; }
  char buf[4096]; int line = 0, bad = 0, seen_run = 0;
  Stmt *st = NULL; size_t nst = 0, capst = 0; int depth = 0, blk[16];
  { int ln = 0; while (fgets(buf, sizeof buf, f)) expand_line(buf, ++ln, &st, &nst, &capst); }
  for (size_t si = 0; si < nst; si++) {
    snprintf(buf, sizeof buf, "%s", st[si].t); line = st[si].line;
    if (buf[0] == 1) { /* block marker from the readability layer */
      int last = sc->n ? sc->v[sc->n - 1].op : -1;
      if (buf[1] == '{') {
        if (last != C_FEATURE && last != C_CUT) { say(LV_ERR, "%s:%d: '{' only follows Feature \"name\" or Cut\n", path, line); bad = 1; continue; }
        if (depth >= 16) { say(LV_ERR, "%s:%d: blocks nested too deep\n", path, line); bad = 1; continue; }
        blk[depth++] = last;
      } else {
        if (!depth) { say(LV_ERR, "%s:%d: '}' without a matching '{'\n", path, line); bad = 1; continue; }
        Cmd e = {.line = line, .op = blk[--depth] == C_CUT ? C_CUTEND : C_FEATEND};
        if (sc->n == sc->cap) { sc->cap = sc->cap ? sc->cap * 2 : 64; sc->v = realloc(sc->v, sc->cap * sizeof(Cmd)); }
        sc->v[sc->n++] = e;
      }
      continue;
    }
    char *tok[12], err[128] = "";
    int n;
    { /* run/setup/cd/env/title take the rest of the line verbatim unless it is one quoted string */
      char *w = buf; while (isspace((unsigned char)*w)) w++;
      size_t wl = strcspn(w, " \t\n");
      if ((wl == 3 && !strncasecmp(w, "run", 3)) || (wl == 5 && !strncasecmp(w, "setup", 5)) || (wl == 2 && !strncasecmp(w, "cd", 2)) || (wl == 3 && !strncasecmp(w, "env", 3))) {
        char *r = w + wl; while (*r == ' ' || *r == '\t') r++;
        size_t rl = strlen(r); while (rl && isspace((unsigned char)r[rl - 1])) r[--rl] = 0;
        if (*r && *r != '#' && *r != '"') { w[wl] = 0; tok[0] = w; tok[1] = r; n = 2; goto tokenized; }
      }
    }
    call_form(buf);
    n = tokenize(buf, tok, 12, err);
    tokenized:
    if (n < 0) { say(LV_ERR, "%s:%d: %s\n", path, line, err); bad = 1; continue; }
    if (!n) continue;
    alias_cmd(tok, &n);
    Cmd c = {.line = line}; int op = -1, argi = 1;
    {
      int k;
      for (k = 0; SPEC[k].name; k++) if (!strcmp(tok[0], SPEC[k].name)) break;
      if (!SPEC[k].name) { say(LV_ERR, "%s:%d: unknown command '%s'\n", path, line, tok[0]); bad = 1; continue; }
      op = SPEC[k].op;
      if (n - 1 < SPEC[k].min || n - 1 > SPEC[k].max) { say(LV_ERR, "%s:%d: %s: wrong argument count (%d..%d)\n", path, line, tok[0], SPEC[k].min, SPEC[k].max); bad = 1; continue; }
    }
    c.op = op; c.n = n - argi;
    for (int i = 0; i < c.n; i++) c.a[i] = strdup(tok[argi + i]);
    int ms, ok = 1;
    switch (op) {
    case C_RUN: seen_run = 1; break;
    case C_TYPE: case C_KEY: case C_CCLICK:
      if (!seen_run) { say(LV_ERR, "%s:%d: input before any Run\n", path, line); ok = 0; }
      if (op == C_TYPE && c.n > 1 && atoi(c.a[1]) < 1) { say(LV_ERR, "%s:%d: type speed must be ms per char >= 1\n", path, line); ok = 0; }
      if (op == C_KEY) for (int i = 0; i < c.n; i++) if (!valid_key(c.a[i])) { say(LV_ERR, "%s:%d: bad key '%s'\n", path, line, c.a[i]); ok = 0; }
      break;
    case C_WAIT: if (dur_ms(c.a[0], &ms)) { say(LV_ERR, "%s:%d: bad duration '%s'\n", path, line, c.a[0]); ok = 0; } break;
    case C_SLIDE: if (c.n == 3 && dur_ms(c.a[2], &ms)) { say(LV_ERR, "%s:%d: bad duration '%s'\n", path, line, c.a[2]); ok = 0; } break;
    case C_TYPING: if (strcmp(c.a[0], "instant") && strcmp(c.a[0], "human")) { say(LV_ERR, "%s:%d: typing instant|human\n", path, line); ok = 0; } break;
    case C_PSCROLL: case C_MSCROLL:
      if (!seen_run) { say(LV_ERR, "%s:%d: scroll before any 'run'\n", path, line); ok = 0; }
      if (strcmp(c.a[0], "up") && strcmp(c.a[0], "down")) { say(LV_ERR, "%s:%d: %s up|down [count]\n", path, line, op == C_PSCROLL ? "PageScroll" : "MouseScroll"); ok = 0; }
      if (c.n == 2 && (atoi(c.a[1]) < 1 || atoi(c.a[1]) > 100)) { say(LV_ERR, "%s:%d: scroll count 1..100\n", path, line); ok = 0; }
      break;
    case C_WAITFOR:
      if (!seen_run) { say(LV_ERR, "%s:%d: WaitFor before any 'run'\n", path, line); ok = 0; }
      if (c.n == 2 && dur_ms(c.a[1], &ms)) { say(LV_ERR, "%s:%d: bad timeout '%s'\n", path, line, c.a[1]); ok = 0; }
      break;
    case C_AUTOCUT: if (strcmp(c.a[0], "off") && strcmp(c.a[0], "on") && atoi(c.a[0]) <= 0) { say(LV_ERR, "%s:%d: AutoCut on|off|MS\n", path, line); ok = 0; } break;
    case C_SPEED: if (atof(c.a[0]) < 0.25 || atof(c.a[0]) > 16) { say(LV_ERR, "%s:%d: speed 0.25..16\n", path, line); ok = 0; } break;
    case C_TRANS: {
      int k = (!strcmp(c.a[0], "in") || !strcmp(c.a[0], "out") || !strcmp(c.a[0], "both")) ? 1 : 0;
      if (c.n <= k) { say(LV_ERR, "%s:%d: Transition [in|out|both] EFFECT [ms]\n", path, line); ok = 0; }
      else if (fx_parse(c.a[k]) < 0) { say(LV_ERR, "%s:%d: transition effect: %s\n", path, line, fx_list()); ok = 0; }
      else if (c.n > k + 1 && dur_ms(c.a[k + 1], &ms)) { say(LV_ERR, "%s:%d: bad duration '%s'\n", path, line, c.a[k + 1]); ok = 0; }
      break; }
    case C_CAPTION:
      if (c.n >= 2 && strcmp(c.a[1], "top") && strcmp(c.a[1], "bottom")) { say(LV_ERR, "%s:%d: caption position top|bottom\n", path, line); ok = 0; }
      break;
    case C_SIZE: { int w, h; if (sscanf(c.a[0], "%dx%d", &w, &h) != 2 || w < 10 || h < 3 || w > 4000 || h > 4000) { say(LV_ERR, "%s:%d: size WxH (cells; pixels for Window gui)\n", path, line); ok = 0; } break; }
    case C_WINDOW:
      if (strcmp(c.a[0], "tui") && strcmp(c.a[0], "gui") && strcmp(c.a[0], "web") && strcmp(c.a[0], "x11")) { say(LV_ERR, "%s:%d: window tui|gui|x11 (web = gui)\n", path, line); ok = 0; }
      break;
    case C_THEME: {
      int tk = 0; for (int q = 0; q < NTHEMES; q++) if (!strcmp(c.a[0], THEMES[q].name)) tk = 1;
      if (!tk) { char tl[256] = ""; for (int q = 0; q < NTHEMES; q++) { strncat(tl, q ? "|" : "", sizeof tl - strlen(tl) - 1); strncat(tl, THEMES[q].name, sizeof tl - strlen(tl) - 1); } say(LV_ERR, "%s:%d: theme %s (add one in src/config.h)\n", path, line, tl); ok = 0; }
      break; }
    case C_FPS: if (atoi(c.a[0]) < 1 || atoi(c.a[0]) > 30) { say(LV_ERR, "%s:%d: fps 1..30\n", path, line); ok = 0; } break;
    case C_FONT: if (atoi(c.a[0]) < 8 || atoi(c.a[0]) > 40) { say(LV_ERR, "%s:%d: font px 8..40\n", path, line); ok = 0; } break;
    case C_ENV: if (!strchr(c.a[0], '=')) { say(LV_ERR, "%s:%d: env KEY=VALUE\n", path, line); ok = 0; } break;
    case C_CMOVE: if (c.n == 3 && dur_ms(c.a[2], &ms)) { say(LV_ERR, "%s:%d: bad duration\n", path, line); ok = 0; } break;
    }
    if (!ok) { bad = 1; continue; }
    if (sc->n == sc->cap) { sc->cap = sc->cap ? sc->cap * 2 : 64; sc->v = realloc(sc->v, sc->cap * sizeof(Cmd)); }
    sc->v[sc->n++] = c;
  }
  fclose(f);
  if (depth) { say(LV_ERR, "%s: '{' never closed\n", path); bad = 1; }
  for (size_t i = 0; i < nst; i++) free(st[i].t);
  free(st);
  return bad ? -1 : 0;
}

#include "fx/draw.c"

#include "fx/text.c"

/* ------------------------------------------------------------ themes ---- */

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

/* ---------------------------------------------------------- recorder ---- */
typedef struct {
  /* config */
  int cols, rows, fps, fontpx, loop, theme, backend, gui, x11, size_set, page_new; char title[128]; Img page;
  char env[16][256]; int nenv; char cwd[512];
  /* canvas */
  int W, H, M, TB, PAD, tx0, ty0;
  /* state */
  Grid gr, prev; int have_session, dirty;
  /* quality gate */
  int autocut; long still, ncut_ms; int cut, ncut_frames, in_feat, feat_seen; const char *only_feat; int human, idle, realtime, dead, dead_status, peak_cells, peak_styles, nstates, ninputs, force, in_run; uint64_t last_hash; char dead_screen[2048];
  Img layer, last, frame; int have_last;
  char caption[256]; int cap_top;
  int ptr_on; double px, py; int ripple;
  char trans[2][16]; int trans_ms[2]; double speed;
  long T; double sleep_scale; int preview;
  /* sinks */
  WebPAnimEncoder *enc; WebPAnimEncoderOptions eopt; WebPConfig wcfg; int lossy;
  FILE *ff; int ff_written, ff_fps; char outfmt; /* w webp, v video */
  struct { long ms; char *file; int done; } snap[8]; int nsnap;
  long nframes; char slide_title[256];
} G;

static void msleep(G *g, int ms) {
  if (ms <= 0) return;
  /* fast-forward: a screen that has not changed for a few polls is idle, so do not really wait (video time still advances) */
  if (!g->realtime && g->idle >= 3) return;
  ms = (int)(ms * g->sleep_scale);
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&ts, NULL);
}
static void tick(G *g, int ms); static void wait_ms(G *g, int ms); static int sp(G *g, int ms); static int jitter(int base); static void render_layer(G *g);
#include "gui.c"
static void write_ppm(const Img *im, const char *path) {
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
  for (int i = 0; i < im->w * im->h; i++) fwrite(im->p + i * 4, 1, 3, f);
  fclose(f);
}
static int vid_flush(G *g, long upto) {
  long target = upto * g->ff_fps / 1000; size_t sz = (size_t)g->last.w * g->last.h * 4;
  while (g->ff_written < target) { if (fwrite(g->last.p, 1, sz, g->ff) != sz) return -1; g->ff_written++; }
  return 0;
}
/* advance video time; a picture that stays unchanged for longer than AutoCut ms stops costing video time */
static void advance(G *g, int dur, int same) {
  if (!same) g->still = dur;
  else if (g->autocut > 0) {
    long room = g->autocut - g->still; if (room < 0) room = 0;
    g->still += dur;
    if (dur > room) { g->ncut_ms += dur - room; dur = (int)room; }
  }
  g->T += dur;
}
/* present image for dur ms: dedup identical frames, only changed ones reach the encoder */
static void put(G *g, const Img *im, int dur) {
  if (g->cut) return; /* Cut ... CutEnd: nothing is recorded, video time stands still */
  size_t sz = (size_t)im->w * im->h * 4;
  if (!g->have_last || memcmp(g->last.p, im->p, sz)) {
    for (int i = 0; i < g->nsnap; i++) if (!g->snap[i].done && g->have_last && g->snap[i].ms < g->T) { write_ppm(&g->last, g->snap[i].file); g->snap[i].done = 1; }
    if (g->ff && g->have_last) vid_flush(g, g->T);
    if (g->enc) {
      WebPPicture pic; WebPPictureInit(&pic); pic.width = im->w; pic.height = im->h; pic.use_argb = 1;
      WebPPictureImportRGBA(&pic, im->p, im->w * 4);
      if (!WebPAnimEncoderAdd(g->enc, &pic, (int)g->T, &g->wcfg)) say(LV_ERR, "webp add failed: %s\n", WebPAnimEncoderGetError(g->enc));
      WebPPictureFree(&pic);
    }
    memcpy(g->last.p, im->p, sz); g->have_last = 1; g->nframes++; advance(g, dur, 0); return;
  }
  advance(g, dur, 1);
}

/* ---- rendering ---- */
static void layout(G *g) {
  const Theme *t = &THEMES[g->theme];
  g->M = CFG_MARGIN; g->TB = CFG_TITLEBAR_H; g->PAD = CFG_PADDING;
  g->W = g->cols * cw + 2 * g->PAD + 2 * g->M; g->H = g->rows * ch + 2 * g->PAD + g->TB + 2 * g->M;
  if (g->gui) { g->PAD = 0; g->W = g->cols + 2 * g->M; g->H = g->rows + g->TB + 2 * g->M; } /* gui: cols x rows are pixels */
  g->W += g->W & 1; g->H += g->H & 1;
  g->tx0 = g->M + g->PAD; g->ty0 = g->M + g->TB + g->PAD;
  (void)t;
}
static void render_layer(G *g) {
  const Theme *t = &THEMES[g->theme]; Img *L = &g->layer;
  img_fill(L, 0, 0, L->w, L->h, t->outer, 255);
  int ww = L->w - 2 * g->M, wh = L->h - 2 * g->M;
  img_fill(L, g->M + CFG_SHADOW_DX, g->M + CFG_SHADOW_DY, ww, wh, CFG_SHADOW_COLOR, CFG_SHADOW_ALPHA);
  int bp = t->accent ? CFG_BORDER_PX : 0;
  if (bp) img_rrect(L, g->M, g->M, ww, wh, CFG_CORNER_R, t->accent, 255);
  img_rrect(L, g->M + bp, g->M + bp, ww - 2 * bp, wh - 2 * bp, CFG_CORNER_R, t->bg, 255);
  img_rrect(L, g->M + bp, g->M + bp, ww - 2 * bp, g->TB - bp, CFG_CORNER_R, t->bar, 255);
  img_fill(L, g->M + bp, g->M + g->TB - CFG_CORNER_R, ww - 2 * bp, CFG_CORNER_R, t->bar, 255);
  if (bp) img_fill(L, g->M + bp, g->M + g->TB, ww - 2 * bp, 1, t->accent, 255); /* line under the title bar */
  img_disc(L, g->M + 16, g->M + g->TB / 2, CFG_DOT_R, CFG_DOT_CLOSE, 255, 0);
  img_disc(L, g->M + 36, g->M + g->TB / 2, CFG_DOT_R, CFG_DOT_MIN, 255, 0);
  img_disc(L, g->M + 56, g->M + g->TB / 2, CFG_DOT_R, CFG_DOT_MAX, 255, 0);
  int tw = text_width(fsans, CFG_TITLE_PX, g->title);
  draw_text(L, fsans, CFG_TITLE_PX, g->M + (ww - tw) / 2, g->M + g->TB / 2 + CFG_TITLE_PX * 2 / 5, g->title, t->accent ? t->accent : t->fg, CFG_TITLE_ALPHA);
  if (g->gui) { gui_blit(g); return; }
  if (!g->have_session) return;
  Grid *gr = &g->gr;
  for (int r = 0; r < gr->rows; r++) for (int c = 0; c < gr->cols; c++) {
    Cell k = gr->c[r * gr->cols + c]; uint8_t at = k.at;
    if (gr->cvis && r == gr->cy && c == gr->cx) at ^= 8;
    uint32_t fg = k.fg, bg = k.bg;
    if (at & 8) { uint32_t x = fg; fg = bg; bg = x; }
    int x = g->tx0 + c * cw, y = g->ty0 + r * ch;
    if (bg != t->bg) img_fill(L, x, y, cw, ch, bg, 255);
    if (k.cp > ' ') {
      int a = (at & 2) ? 140 : 255;
      draw_glyph(L, (at & 1) ? fmonob : fmono, k.cp, x, y + base, fg, a, 0);
    }
    if (at & 4) img_fill(L, x, y + base + 2, cw, 1, fg, 255);
  }
}
static void compose(G *g) {
  Img *F = &g->frame; memcpy(F->p, g->layer.p, (size_t)F->w * F->h * 4);
  if (g->ptr_on) {
    static const char *arrow[] = {"X","XX","XWX","XWWX","XWWWX","XWWWWX","XWWWWWX","XWWWWWWX","XWWWWWWWX","XWWWWWWWWX","XWWWWWXXXXX","XWWXWWX","XWX XWWX","XX  XWWX","X    XWWX","     XWWX","      XX",0};
    int ox = (int)g->px, oy = (int)g->py;
    if (g->ripple) img_disc(F, ox, oy, 6 + g->ripple * 7, CFG_RIPPLE_COLOR, 200 - g->ripple * 40, 2.5);
    for (int j = 0; arrow[j]; j++) for (int i = 0; arrow[j][i]; i++) {
      char c = arrow[j][i]; if (c == ' ') continue;
      img_fill(F, ox + i * 2, oy + j * 2, 2, 2, c == 'X' ? CFG_POINTER_OUTLINE : CFG_POINTER_FILL, 255);
    }
  }
  if (g->caption[0]) {
    int px = CFG_CAPTION_PX, tw = text_width(fsansb, px, g->caption), bw = tw + 36, bh = px + 22;
    if (bw > F->w - 20) bw = F->w - 20;
    int bx = (F->w - bw) / 2, by = g->cap_top ? g->M + g->TB + 14 : F->h - g->M - bh - 14;
    img_rrect(F, bx, by, bw, bh, 10, CFG_CAPTION_BG, CFG_CAPTION_BG_ALPHA);
    draw_text(F, fsansb, px, bx + (bw - tw) / 2, by + bh / 2 + px / 3, g->caption, CFG_CAPTION_FG, 255);
  }
}
static void present_dirty(G *g, int dur) {
  if (g->cut) return;
  if (g->dirty || !g->have_last) { compose(g); g->dirty = 0; put(g, &g->frame, dur); }
  else advance(g, dur, 1);
}

/* ---- capture ---- */

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

/* ---- transitions and slides: src/fx/anim.c ---- */
static int sp(G *g, int ms) { if (ms <= 0) return 0; int v = (int)(ms / g->speed + 0.5); return v < 1 ? 1 : v; }
#include "fx/anim.c"

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

/* ---- backends (interface for phase 2) ----
 * A backend turns "input events" into either a cell Grid (tui) or a pixel Img
 * (gui/web). Only tui is implemented; gui = headless wlroots compositor + wtype + grim,
 * web = headless Firefox screenshots. They are selected with `window gui|web`. */
static int backend_available(const char *name) {
  (void)name; return 1; /* gui and web are the same: a real program in the built-in compositor */
}

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

static int usage(void) {
  fprintf(stderr, "usage: autodemo render X.demo [-o OUT.(webp|mp4|webm)|FORMAT] [-q N] [-v] [--feature NAME] [--split] [--force] [--realtime] [--snap MS FILE.ppm]\n"
                  "       autodemo preview X.demo      (run and dump key screens as ASCII)\n"
                  "       autodemo check X.demo        (validate the script)\n"
                  "       autodemo export NAME[:FEATURE].fmt... [--split]   (NAME from .autodemo/examples; --split = one file per Feature)\n"
                  "       autodemo gen PROGRAM [-d DIR] [-c CMD]...  (write .demo from --help + man EXAMPLES, or from your -c commands);  autodemo auto PROGRAM = gen + render\n"
                  "       autodemo features X.demo     (list the Feature sections)\n"
                  "       autodemo bulk export [fmt] [NAME...] | check | preview | clean | list\n"
                  "  no -o: writes .autodemo/export/NAME.<CFG_FORMAT> next to .autodemo/examples (dirs are created)\n  -q N   webp: 0 = lossless, 1..100 = lossy; default CFG_QUALITY quality\n");
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
    if (feature) { char sl[128]; feat_slug(feature, sl, sizeof sl); snprintf(out, sizeof out, "%s/.autodemo/export/%s.%s.%s", root, name, sl, fmt); a[n++] = "--feature"; a[n++] = (char *)feature; }
    else snprintf(out, sizeof out, "%s/.autodemo/export/%s.%s", root, name, fmt);
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
        char p[1300]; snprintf(p, sizeof p, "%s/.autodemo/export/%s.%s", root, names[k], *f);
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

int main(int argc, char **argv) {
  if (argc >= 5 && !strcmp(argv[1], "sandbox")) { /* sandbox WORKDIR net|nonet CMD : run CMD with a private view of the system */
    int rc = sandbox_run(argv[2], !strcmp(argv[3], "net"), argv[4], argc > 5 ? argv[5] : NULL); if (rc < 0) { say(LV_ERR, "sandbox unavailable here\n"); return 125; } return rc;
  }
  if (argc >= 3 && (!strcmp(argv[1], "gen") || !strcmp(argv[1], "auto"))) { /* gen PROGRAM [-d DIR] [-c COMMAND]... */
    char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) { perror("readlink"); return 1; }
    self[l] = 0; const char *od = NULL; char *cu[8]; int nc = 0;
    for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "-d") && i + 1 < argc) od = argv[++i]; else if (!strcmp(argv[i], "-c") && i + 1 < argc && nc < 8) cu[nc++] = argv[++i]; else return usage(); }
    return gen_cmd(self, argv[2], argv[1][0] == 'a', od, cu, nc);
  }
  if (argc >= 2 && (!strcmp(argv[1], "export") || !strcmp(argv[1], "bulk") || !strcmp(argv[1], "list"))) {
    char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) { perror("readlink"); return 1; }
    self[l] = 0; return project_cmd(self, argc, argv);
  }
  if (argc < 3) return usage();
  const char *mode = argv[1], *in = argv[2], *out = NULL; int q = CFG_QUALITY, split = 0;
  G *g = calloc(1, sizeof *g);
  for (int i = 3; i < argc; i++) {
    if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
    else if (!strcmp(argv[i], "-q") && i + 1 < argc) q = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) verbose = 1;
    else if (!strcmp(argv[i], "--force")) g->force = 1;
    else if (!strcmp(argv[i], "--feature") && i + 1 < argc) g->only_feat = argv[++i];
    else if (!strcmp(argv[i], "--split")) split = 1;
    else if (!strcmp(argv[i], "--realtime")) g->realtime = 1;
    else if (!strcmp(argv[i], "--speed") && i + 1 < argc) cli_speed = atof(argv[++i]);
    else if (!strcmp(argv[i], "--snap") && i + 2 < argc && g->nsnap < 8) { g->snap[g->nsnap].ms = atol(argv[++i]); g->snap[g->nsnap++].file = argv[++i]; }
    else return usage();
  }
  Script sc = {0};
  if (parse_script(in, &sc)) return 1;
  apply_header(g, &sc);
  if (cli_speed > 0) g->speed = cli_speed;
  if (!strcmp(mode, "features")) {
    int nf = 0; for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE) { printf("%s\n", sc.v[i].a[0]); nf++; }
    if (!nf) say(LV_NOTE, "%s has no Feature lines\n", in);
    return 0;
  }
  if (g->only_feat) { int f = 0; for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE && feat_match(sc.v[i].a[0], g->only_feat)) f = 1;
    if (!f) { say(LV_ERR, "%s has no feature '%s' (autodemo features %s)\n", in, g->only_feat, in); return 1; } }
  if (!strcmp(mode, "check")) {
    printf("%s: ok, %d commands, %dx%d cells, ~%.1fs\n", in, sc.n, g->cols, g->rows, estimate_ms(&sc) / 1000.0);
    return 0;
  }
  int render = !strcmp(mode, "render"), preview = !strcmp(mode, "preview");
  if (!render && !preview) return usage();
  char outbuf[1024];
  if (render) {
    if (out && (!strcmp(out, "webp") || !strcmp(out, "mp4") || !strcmp(out, "webm"))) { /* bare format name */
      snprintf(outbuf, sizeof outbuf, "%s", out); out = NULL; fmt_ext = strdup(outbuf);
    }
    if (!out) { /* convention: <project>/.autodemo/export/<name>.<fmt>, project = parent of .autodemo/examples */
      char dir[1024], base[256]; snprintf(dir, sizeof dir, "%s", in);
      char *sl = strrchr(dir, '/'); const char *bn = sl ? sl + 1 : dir; if (sl) *sl = 0; else strcpy(dir, ".");
      snprintf(base, sizeof base, "%s", bn); char *dot = strrchr(base, '.'); if (dot) *dot = 0;
      size_t dl = strlen(dir); const char *tail = "/examples";
      if (dl >= strlen(tail) && !strcmp(dir + dl - strlen(tail), tail)) dir[dl - strlen(tail)] = 0; /* .autodemo */
      else { strncat(dir, "/.autodemo", sizeof dir - strlen(dir) - 1); }
      char fs[140] = ""; if (g->only_feat) { char sl[128]; feat_slug(g->only_feat, sl, sizeof sl); snprintf(fs, sizeof fs, ".%s", sl); }
      snprintf(outbuf, sizeof outbuf, "%s/export/%s%s.%s", dir, base, fs, fmt_ext ? fmt_ext : CFG_FORMAT); out = outbuf;
    } else if (!strrchr(strrchr(out, '/') ? strrchr(out, '/') : out, '.')) {
      snprintf(outbuf, sizeof outbuf, "%s." CFG_FORMAT, out); say(LV_ERR, "no extension in -o, writing %s\n", outbuf); out = outbuf;
    }
    mkdir_p(out);
    if (split) { /* one render per Feature: NAME.FEATURE.ext next to the chosen output */
      char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1); if (l <= 0) { perror("readlink"); return 1; } self[l] = 0;
      char base[1024]; snprintf(base, sizeof base, "%s", out); char *dot = strrchr(base, '.'); const char *ext = dot ? dot : "." CFG_FORMAT; char extb[16]; snprintf(extb, sizeof extb, "%s", ext); if (dot) *dot = 0;
      int nf = 0, bad = 0, skipped = 0;
      for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_FEATURE) {
        char sl[128], o2[1200], qs[16], ss[32]; feat_slug(sc.v[i].a[0], sl, sizeof sl); snprintf(o2, sizeof o2, "%s.%s%s", base, sl, extb);
        char *a[16]; int n = 0; a[n++] = self; a[n++] = "render"; a[n++] = (char *)in; a[n++] = "--feature"; a[n++] = sc.v[i].a[0]; a[n++] = "-o"; a[n++] = o2;
        if (q) { snprintf(qs, sizeof qs, "%d", q); a[n++] = "-q"; a[n++] = qs; }
        if (cli_speed > 0) { snprintf(ss, sizeof ss, "%g", cli_speed); a[n++] = "--speed"; a[n++] = ss; }
        if (g->force) a[n++] = "--force";
        a[n] = NULL; nf++;
        say(LV_NOTE, "feature %s\n", sc.v[i].a[0]);
        int rc = run_self(self, a); if (rc == 3) skipped++; else if (rc) bad++;
      }
      if (!nf) { say(LV_ERR, "--split: %s has no Feature lines\n", in); return 1; }
      say(bad ? LV_ERR : LV_OK, "split: %d feature(s), %d failed, %d skipped\n", nf, bad, skipped);
      return bad != 0;
    }
  }
  for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_WINDOW && !backend_available(sc.v[i].a[0])) return 3;
  if (fonts_init(g->fontpx, &THEMES[g->theme])) { say(LV_ERR, "no usable monospace TTF found (set AUTODEMO_FONT=/path.ttf)\n"); return 1; }
  layout(g);
  g->preview = preview; g->sleep_scale = preview ? 0.3 : 1.0;
  int gc = g->gui ? 1 : g->cols, gr_ = g->gui ? 1 : g->rows; /* gui: pixels, no cell grid */
  g->gr = (Grid){gc, gr_, 0, 0, 0, calloc(gc * gr_, sizeof(Cell))};
  g->prev = g->gr; g->prev.c = calloc(gc * gr_, sizeof(Cell));
  g->layer = img_new(g->W, g->H); g->last = img_new(g->W, g->H); g->frame = img_new(g->W, g->H);
  render_layer(g); g->dirty = 1;

  /* private tmux server + private HOME: never touches the real user's config/sessions */
  snprintf(tm_sock, sizeof tm_sock, "autodemo-%d", getpid());
  snprintf(tm_home, sizeof tm_home, "/tmp/autodemo-home-XXXXXX");
  if (!mkdtemp(tm_home)) { perror("mkdtemp"); return 1; }
  snprintf(tm_conf, sizeof tm_conf, "%s/tmux.conf", tm_home);
  FILE *cf = fopen(tm_conf, "w");
  fprintf(cf, "set -g status off\nset -g remain-on-exit on\nset -g escape-time 0\nset -g default-terminal xterm-256color\nset -g history-limit 2000\n");
  fclose(cf);
  char xdg[160];
  setenv("HOME", tm_home, 1); unsetenv("TMUX"); setenv("LANG", "C.UTF-8", 1); setenv("LC_ALL", "C.UTF-8", 1);
  snprintf(xdg, sizeof xdg, "%s/.config", tm_home); setenv("XDG_CONFIG_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/share", tm_home); setenv("XDG_DATA_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/state", tm_home); setenv("XDG_STATE_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.cache", tm_home); setenv("XDG_CACHE_HOME", xdg, 1);
  { /* $AUTODEMO_ROOT = project root (parent of .autodemo/examples), so demos can run ./bin/x without absolute paths */
    char rp[PATH_MAX], *sl;
    if (realpath(in, rp) && (sl = strrchr(rp, '/'))) {
      *sl = 0; size_t l = strlen(rp); const char *t = "/.autodemo/examples";
      if (l >= strlen(t) && !strcmp(rp + l - strlen(t), t)) rp[l - strlen(t)] = 0;
      setenv("AUTODEMO_ROOT", rp, 1);
    }
  }
  setenv("PS1", "$ ", 1); setenv("COLORTERM", "truecolor", 1);
  if (g->nenv < 16 && !getenv("AUTODEMO_KEEP_ENV")) {}

  const char *ext = out ? strrchr(out, '.') : NULL;
  if (render && ext && (!strcmp(ext, ".mp4") || !strcmp(ext, ".webm"))) {
    char cmd[1024]; g->ff_fps = CFG_VIDEO_FPS;
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - -an %s -pix_fmt yuv420p '%s'", g->W, g->H, CFG_VIDEO_FPS,
             !strcmp(ext, ".mp4") ? CFG_MP4_ARGS : CFG_WEBM_ARGS, out);
    g->ff = popen(cmd, "w"); if (!g->ff) { say(LV_ERR, "ffmpeg not available\n"); return 1; }
    g->outfmt = 'v';
  } else if (render) {
    for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_LOOP) g->loop = 1; /* before the encoder exists, or the loop flag is lost */
    WebPAnimEncoderOptionsInit(&g->eopt); g->eopt.anim_params.loop_count = g->loop ? 0 : 1; g->eopt.minimize_size = 1; g->eopt.allow_mixed = 1;
    WebPConfigInit(&g->wcfg); g->wcfg.lossless = q == 0; g->wcfg.quality = q ? q : 90; g->wcfg.method = CFG_WEBP_METHOD;
    if (q == 0) g->wcfg.exact = 0;
    g->enc = WebPAnimEncoderNew(g->W, g->H, &g->eopt); g->outfmt = 'w';
  }

  int rc = execute(g, &sc);
  if (g->gui) gui_close(g);
  else if (g->have_session) TMUX("kill-server");
  if (rc) return 1;
  int bad = gate_verdict(g);
  if (bad && !g->force) {
    say(LV_ERR, "not writing %s (%d problem%s). Fix the demo, or pass --force to keep it.\n", out ? out : "output", bad, bad == 1 ? "" : "s");
    return 4;
  }
  /* hold the last frame briefly so loops do not snap */
  present_dirty(g, 600);
  for (int i = 0; i < g->nsnap; i++) if (!g->snap[i].done && g->have_last) write_ppm(&g->last, g->snap[i].file);
  if (g->ff) { vid_flush(g, g->T); if (pclose(g->ff)) { say(LV_ERR, "ffmpeg failed\n"); return 1; } }
  if (g->enc) {
    WebPAnimEncoderAdd(g->enc, NULL, (int)g->T, NULL);
    WebPData data; WebPDataInit(&data);
    if (!WebPAnimEncoderAssemble(g->enc, &data)) { say(LV_ERR, "webp assemble failed\n"); return 1; }
    FILE *f = fopen(out, "wb"); if (!f || fwrite(data.bytes, 1, data.size, f) != data.size) { perror(out); return 1; }
    fclose(f); WebPDataClear(&data); WebPAnimEncoderDelete(g->enc);
  }
  if (render) {
    FILE *f = fopen(out, "rb"); long sz = 0; if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
    printf("%s: %dx%d, %.1fs, %ld unique frames, %.1f KiB\n", out, g->W, g->H, g->T / 1000.0, g->nframes, sz / 1024.0); if (g->ncut_ms > 0) say(LV_NOTE, "AutoCut removed %.1fs of still picture\n", g->ncut_ms / 1000.0);
    /* GitHub check: README media is inlined up to ~10 MB; big files are slow to load and bloat the repo */
    double mib = sz / 1048576.0;
    if (mib > CFG_GH_MAX_MIB && !g->force) { say(LV_ERR, "github: %.1f MiB is over the 10 MiB README media limit, it will not be shown. Shorten (Speed, Loop a live UI), shrink Size/Font, use -q 60 or mp4/webm. Output removed (--force keeps it).\n", mib); unlink(out); return 5; }
    if (mib > CFG_GH_MAX_MIB) say(LV_ERR, "github: %.1f MiB is over the 10 MiB README media limit\n", mib);
    else if (mib > CFG_GH_WARN_MIB) say(LV_WARN, "github: %.1f MiB is heavy for a README (limit 10 MiB); consider -q 60 or mp4/webm\n", mib);
    else if (mib > CFG_GH_NOTE_MIB) say(LV_NOTE, "github: %.1f MiB (fine, but slow on mobile)\n", mib);
  }
  return 0;
}
