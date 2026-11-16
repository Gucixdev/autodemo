/* parse.c - the .demo script: Cmd/Script, tokenizer, the readability layer (; and several statements per line) and validation.
 * Included by autodemo.c after say(), the themes and the effect names. Nothing here touches tmux or the screen. */
/* ------------------------------------------------------------ script ---- */
enum { C_RUN, C_TYPE, C_KEY, C_WAIT, C_CAPTION, C_SLIDE, C_TRANS, C_CMOVE, C_CCLICK,
       C_CHIDE, C_CSHOW, C_WINDOW, C_THEME, C_SIZE, C_LOOP, C_FPS, C_FONT, C_TITLE,
       C_ENV, C_CD, C_SETUP, C_SPEED, C_AUTOCUT, C_TYPING, C_CUT, C_CUTEND, C_WAITFOR, C_FEATURE, C_FEATEND, C_PSCROLL, C_MSCROLL, C_NOP };
typedef struct { int op, line, n; char *a[6]; } Cmd;
typedef struct { Cmd *v; int n, cap; } Script;

static const struct { const char *name; int op, min, max; } SPEC[] = {
  {"run",C_RUN,1,1},{"send",C_TYPE,1,2},{"sendkey",C_KEY,1,5},{"sleep",C_WAIT,1,1},{"mousemove",C_CMOVE,2,3},{"mouseclick",C_CCLICK,0,0},{"mousehide",C_CHIDE,0,0},{"mouseshow",C_CSHOW,0,0},
  {"caption",C_CAPTION,1,3},{"slide",C_SLIDE,1,3},{"transition",C_TRANS,1,3},{"speed",C_SPEED,1,1},{"autocut",C_AUTOCUT,1,1},{"typing",C_TYPING,1,1},{"cut",C_CUT,0,0},{"cutend",C_CUTEND,0,0},{"waitfor",C_WAITFOR,1,3},{"feature",C_FEATURE,1,1},{"pagescroll",C_PSCROLL,1,2},{"mousescroll",C_MSCROLL,1,2},
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
      if (c.n >= 2 && !strcmp(c.a[0], "title")) { if (c.n == 3 && dur_ms(c.a[2], &ms)) { say(LV_ERR, "%s:%d: bad timeout '%s'\n", path, line, c.a[2]); ok = 0; } }   /* Window gui: title "text" [timeout] */
      else if (c.n >= 2 && !strcmp(c.a[0], "settle")) {                                                                                                  /* Window gui: settle MS [timeout] */
        if (dur_ms(c.a[1], &ms) || ms < 1) { say(LV_ERR, "%s:%d: WaitFor settle needs a duration (e.g. 500ms)\n", path, line); ok = 0; }
        else if (c.n == 3 && dur_ms(c.a[2], &ms)) { say(LV_ERR, "%s:%d: bad timeout '%s'\n", path, line, c.a[2]); ok = 0; } }
      else if (c.n == 3) { say(LV_ERR, "%s:%d: WaitFor \"text\" [timeout] (Window gui: WaitFor title \"text\" [timeout] | WaitFor settle MS [timeout])\n", path, line); ok = 0; }
      else if (c.n == 2 && dur_ms(c.a[1], &ms)) { say(LV_ERR, "%s:%d: bad timeout '%s'\n", path, line, c.a[1]); ok = 0; }
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
