/* leak.c - privacy gate: does the recording show who made it? Included after record.c (needs G), before gate.c (the verdict reports it).
 * Terminal demos: the exact screen text of every new screen is scanned. Picture (gui) demos: sampled OCR through tesseract, when it is installed.
 * Looked for: the real login name, real home path, host name and real name of the account, e-mail addresses, token shapes, "password=" style lines.
 * A name or token is an error (exit 4, --force keeps the output anyway); a keyword is a warning. Findings print masked. */
#include <pwd.h>
#include <unistd.h>

static const char *ci_find(const char *h, const char *n) {
  size_t nl = strlen(n); if (!nl) return NULL;
  for (; *h; h++) if (!strncasecmp(h, n, nl)) return h;
  return NULL;
}
static char LK_NEEDLE[8][96]; static int LK_NNEEDLE = -1;
static void leak_needles(void) {
  if (LK_NNEEDLE >= 0) return;
  LK_NNEEDLE = 0;
  struct passwd *pw = getpwuid(getuid()); char host[96] = "";
  gethostname(host, sizeof host - 1);
  const char *cand[4] = {pw ? pw->pw_name : "", pw && strcmp(pw->pw_dir, "/") ? pw->pw_dir : "", host, ""};
  for (int i = 0; i < 3; i++) if (strlen(cand[i]) >= 3 && strcmp(cand[i], "localhost") && strcmp(cand[i], "/root") && LK_NNEEDLE < 8) snprintf(LK_NEEDLE[LK_NNEEDLE++], 96, "%s", cand[i]);
  if (pw && pw->pw_gecos) {                       /* real name: every word of the first GECOS field */
    char g[160]; snprintf(g, sizeof g, "%s", pw->pw_gecos); char *c = strchr(g, ','); if (c) *c = 0;
    for (char *w = strtok(g, " "); w && LK_NNEEDLE < 8; w = strtok(NULL, " ")) if (strlen(w) >= 4) snprintf(LK_NEEDLE[LK_NNEEDLE++], 96, "%s", w);
  }
}
static void leak_add(G *g, int err, const char *kind, const char *hit) {
  char s[48]; size_t n = strlen(hit);
  if (!err) snprintf(s, sizeof s, "%.40s", hit);
  else if (n <= 8) snprintf(s, sizeof s, "%s", "****");
  else snprintf(s, sizeof s, "%.4s…", hit);                    /* masked: the report must not leak it again */
  for (int i = 0; i < g->nleak; i++) if (!strcmp(g->leak_kind[i], kind) && !strcmp(g->leak_hit[i], s)) return;
  if (g->nleak >= 12) return;
  snprintf(g->leak_kind[g->nleak], sizeof g->leak_kind[0], "%s", kind); snprintf(g->leak_hit[g->nleak], sizeof g->leak_hit[0], "%s", s);
  g->leak_err[g->nleak] = err; g->leak_at[g->nleak] = g->T; g->nleak++;
}
static int isw(int c) { return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'; }
/* text: one screen (or one OCR result). */
static void leak_text(G *g, const char *t) {
  leak_needles();
  for (int i = 0; i < LK_NNEEDLE; i++) if (ci_find(t, LK_NEEDLE[i])) leak_add(g, 1, i == 0 ? "login name" : "account name, home or host", "(hidden)");
  for (const char *p = t; (p = strchr(p, '@')); p++) {                            /* e-mail: word@word.tld */
    const char *a = p; while (a > t && isw(a[-1])) a--;
    const char *b = p + 1; while (isw(*b)) b++;
    if (a == p || b == p + 1 || !memchr(p + 1, '.', (size_t)(b - p - 1))) continue;
    char m[96]; snprintf(m, sizeof m, "%.*s", (int)(b - a) < 95 ? (int)(b - a) : 95, a);
    if (!ci_find(m, "noreply") && !ci_find(m, "example.")) leak_add(g, 1, "e-mail address", m);
  }
  static const char *pre[] = {"ghp_", "gho_", "ghs_", "github_pat_", "glpat-", "xoxb-", "xoxp-", "AKIA", "sk-ant-", "sk-", "-----BEGIN", "hf_", NULL};
  for (int i = 0; pre[i]; i++) for (const char *p = t; (p = strstr(p, pre[i])); p++) {
    if (p > t && isalnum((unsigned char)p[-1])) continue;                         /* inside a word: "task-", "disk-" */
    const char *e = p + strlen(pre[i]); int n = 0; while (isalnum((unsigned char)e[n]) || e[n] == '_' || e[n] == '-') n++;
    if (n >= 16 || (i == 10 && n >= 0)) { char m[64]; snprintf(m, sizeof m, "%.*s", (int)(e - p) + (n < 24 ? n : 24), p); leak_add(g, 1, "token or key", m); }
  }
  static const char *kw[] = {"password", "passwd", "secret", "api_key", "apikey", "token", NULL};
  for (int i = 0; kw[i]; i++) for (const char *p = t; (p = ci_find(p, kw[i])); p++) {
    const char *e = p + strlen(kw[i]); while (*e == ' ') e++;
    if ((*e == '=' || *e == ':') && e[1] && e[1] != '\n') { char m[48]; snprintf(m, sizeof m, "%s%c ...", kw[i], *e); leak_add(g, 0, "keyword with a value", m); }
  }
}
/* terminal: the grid as text */
static void leak_grid(G *g) {
  size_t n = (size_t)g->gr.cols * g->gr.rows; char *t = malloc(n + (size_t)g->gr.rows + 1); if (!t) return; size_t k = 0;
  for (int y = 0; y < g->gr.rows; y++) { for (int x = 0; x < g->gr.cols; x++) { uint32_t cp = g->gr.c[y * g->gr.cols + x].cp; t[k++] = cp >= 32 && cp < 127 ? (char)cp : ' '; } t[k++] = '\n'; }
  t[k] = 0; leak_text(g, t); free(t);
}
/* picture: OCR, at most one picture per 1.2 s of video time and 60 in all (tesseract takes about a second a picture) */
static void leak_img(G *g, const Img *im) {
  static int have = -1, nocr; static long last = -100000;
  if (!g->in_run || !im->p) return;
  if (have < 0) { have = system("command -v tesseract >/dev/null 2>&1") == 0; if (!have) say(LV_NOTE, "leak scan of pictures skipped: tesseract not found (terminal demos are scanned exactly)\n"); }
  if (!have || nocr >= 60 || g->T - last < 1200) return;
  last = g->T; nocr++;
  char f[] = "/tmp/autodemo-ocr-XXXXXX"; int fd = mkstemp(f); if (fd < 0) return;
  FILE *o = fdopen(fd, "wb"); if (!o) { close(fd); return; }
  fprintf(o, "P6\n%d %d\n255\n", im->w * 2, im->h * 2);                          /* 2x, nearest: small UI text reads much better */
  for (int y = 0; y < im->h * 2; y++) for (int x = 0; x < im->w * 2; x++) fwrite(im->p + ((size_t)(y / 2) * im->w + x / 2) * 4, 1, 3, o);
  fclose(o);
  char cmd[160]; snprintf(cmd, sizeof cmd, "tesseract %s - --psm 11 2>/dev/null", f);
  FILE *p = popen(cmd, "r");
  if (p) { char *t = malloc(1 << 16); size_t n = t ? fread(t, 1, (1 << 16) - 1, p) : 0; if (t) { t[n] = 0; leak_text(g, t); free(t); } pclose(p); }
  unlink(f);
}
/* verdict part: number of errors */
static int leak_verdict(G *g) {
  int err = 0;
  for (int i = 0; i < g->nleak; i++) {
    say(g->leak_err[i] ? LV_ERR : LV_WARN, "%s \"%s\" on screen at %.1fs%s\n", g->leak_kind[i], g->leak_hit[i], g->leak_at[i] / 1000.0, g->gui ? " (found by OCR, check by eye)" : "");
    err += g->leak_err[i];
  }
  return err;
}
