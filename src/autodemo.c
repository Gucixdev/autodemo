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
#include <ftw.h>
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

#include "parse.c"
#include "fx/draw.c"
#include "fx/text.c"
#include "term.c"
#include "fx/chrome.c"
#include "record.c"
#include "leak.c"
#include "gate.c"
#include "fx/anim.c"
#include "input.c"
#include "session.c"
#include "exec.c"
#include "cli.c"

/* The private HOME (tmux conf, toolkit caches, a Firefox profile can reach 100 MB) is removed when autodemo exits; it used to leak into /tmp (tmpfs) on every run. */
static int home_rm(const char *path, const struct stat *sb, int type, struct FTW *ft) { (void)sb; (void)type; (void)ft; remove(path); return 0; }
static void home_cleanup(void) { if (!strncmp(tm_home, "/tmp/autodemo-home-", 19) && !strstr(tm_home, "..")) nftw(tm_home, home_rm, 16, FTW_DEPTH | FTW_PHYS); }

#include "rec.c"

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "rec")) return rec_cmd(argc, argv); /* rec OUT.demo [-s SETUP]... [--mouse] [--size CxR] [--] [CMD...] */
  if (argc >= 5 && !strcmp(argv[1], "sandbox")) { /* sandbox WORKDIR net|nonet CMD : run CMD with a private view of the system */
    int rc = sandbox_run(argv[2], !strcmp(argv[3], "net"), argv[4], argc > 5 ? argv[5] : NULL); if (rc < 0) { say(LV_ERR, "sandbox unavailable here\n"); return 125; } return rc;
  }
  if (argc >= 3 && (!strcmp(argv[1], "gen") || !strcmp(argv[1], "auto"))) { /* gen PROGRAM [-d DIR] [-c COMMAND]... */
    char self[1024]; ssize_t l = readlink("/proc/self/exe", self, sizeof self - 1);
    if (l <= 0) { perror("readlink"); return 1; }
    self[l] = 0; const char *od = NULL; char *cu[8]; int nc = 0;
    char *su[8]; int ns = 0;
    for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "-d") && i + 1 < argc) od = argv[++i]; else if (!strcmp(argv[i], "-c") && i + 1 < argc && nc < 8) cu[nc++] = argv[++i]; else if (!strcmp(argv[i], "-s") && i + 1 < argc && ns < 8) su[ns++] = argv[++i]; else return usage(); }
    return gen_cmd(self, argv[2], argv[1][0] == 'a', od, cu, nc, su, ns);
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
    if (!out) { /* convention: next to the demo, <name>.<fmt> (hand-written and generated demos live in .autodemo/examples) */
      char dir[1024], base[256]; snprintf(dir, sizeof dir, "%s", in);
      char *sl = strrchr(dir, '/'); const char *bn = sl ? sl + 1 : dir; if (sl) *sl = 0; else strcpy(dir, ".");
      snprintf(base, sizeof base, "%s", bn); char *dot = strrchr(base, '.'); if (dot) *dot = 0;
      char fs[140] = ""; if (g->only_feat) { char sl2[128]; feat_slug(g->only_feat, sl2, sizeof sl2); snprintf(fs, sizeof fs, ".%s", sl2); }
      snprintf(outbuf, sizeof outbuf, "%s/%s%s.%s", dir, base, fs, fmt_ext ? fmt_ext : CFG_FORMAT); out = outbuf;
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
  atexit(home_cleanup);
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
      *sl = 0; const char *t = "/.autodemo/examples"; char *at = strstr(rp, t);   /* also from a subfolder of examples */
      if (at && (!at[strlen(t)] || at[strlen(t)] == '/')) *at = 0;
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
  for (int i = 0; i < sc.n; i++) if (sc.v[i].op == C_AUDIO) { /* sound track: mux after the picture is done, so a failed demo never leaves a half-made file */
    if (!g->ff) { say(LV_NOTE, "Audio ignored: a webp has no sound (render to mp4 or webm)\n"); break; }
    char ap[1100], tmp[1100], fade[160], vol[64], dur[32]; const char *sl = strrchr(in, '/'); const char *fa = sc.v[i].a[0];
    if (fa[0] == '/' || !sl) snprintf(ap, sizeof ap, "%s", fa); else snprintf(ap, sizeof ap, "%.*s/%s", (int)(sl - in), in, fa);
    snprintf(tmp, sizeof tmp, "%s.audio%s", out, ext);
    snprintf(vol, sizeof vol, "volume=%s", sc.v[i].n == 2 ? sc.v[i].a[1] : "1"); snprintf(dur, sizeof dur, "%.3f", g->T / 1000.0);
    snprintf(fade, sizeof fade, "%s,afade=t=out:st=%.3f:d=1.5", vol, g->T / 1000.0 > 1.5 ? g->T / 1000.0 - 1.5 : 0.0);
    int rc = run_self("/usr/bin/env", (char *[]){"env", "ffmpeg", "-loglevel", "error", "-y", "-i", (char *)out, "-stream_loop", "-1", "-i", ap, "-t", dur, "-map", "0:v", "-map", "1:a", "-c:v", "copy",
             "-c:a", (char *)(!strcmp(ext, ".webm") ? "libopus" : "aac"), "-af", fade, tmp, NULL});
    if (rc) { say(LV_ERR, "ffmpeg could not add %s (the picture-only file is kept)\n", ap); unlink(tmp); return 1; }
    if (rename(tmp, out)) { perror(out); return 1; }
    say(LV_NOTE, "audio: %s under %.1fs of video\n", fa, g->T / 1000.0); break; }
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
