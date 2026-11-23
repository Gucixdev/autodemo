/* rec.c - `autodemo rec OUT.demo`: you play the scenario by hand, autodemo writes the .demo.
 * The program runs in a private tmux (private HOME, like a render), your keys go through a pty and are logged with their timing;
 * Ctrl+] (or the program ending) stops. The log becomes Send / SendKey / Sleep (and MouseMove/MouseClick/MouseScroll with --mouse)
 * lines under `Typing human`, so the replay types like a person but the pauses you really made stay. Edit the file, then render it. */
#include <pty.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>

typedef struct { FILE *o; char txt[4096]; int nt; char key[5][24]; int rep[5]; int nk; int mx, my, px, py; } Rec;

static long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000L; }

static void rec_text(Rec *r) {
  if (!r->nt) return;
  fputs("Send \"", r->o);
  for (int i = 0; i < r->nt; i++) { unsigned char c = (unsigned char)r->txt[i];
    if (c == '"' || c == '\\') { fputc('\\', r->o); fputc(c, r->o); } else if (c == '\n') fputs("\\n", r->o); else fputc(c, r->o); }
  fputs("\"\n", r->o); r->nt = 0;
}
static void rec_keys(Rec *r) {
  if (!r->nk) return;
  fputs("SendKey", r->o);
  for (int i = 0; i < r->nk; i++) { if (r->rep[i] > 1) fprintf(r->o, " %s*%d", r->key[i], r->rep[i]); else fprintf(r->o, " %s", r->key[i]); }
  fputc('\n', r->o); r->nk = 0;
}
static void rec_flush(Rec *r) { rec_text(r); rec_keys(r); }
static void rec_key(Rec *r, const char *name) {
  rec_text(r);
  if (r->nk && !strcmp(r->key[r->nk - 1], name)) { r->rep[r->nk - 1]++; return; }
  if (r->nk == 5) rec_keys(r);
  snprintf(r->key[r->nk], sizeof r->key[0], "%s", name); r->rep[r->nk++] = 1;
}
static void rec_char(Rec *r, unsigned char c) {
  rec_keys(r);
  if (r->nt < (int)sizeof r->txt - 2) r->txt[r->nt++] = (char)c; else { rec_text(r); r->txt[r->nt++] = (char)c; }
}
static void rec_sleep(Rec *r, long ms) { rec_flush(r); ms = (ms + 50) / 100 * 100; if (ms > 0) fprintf(r->o, "Sleep %ldms\n", ms); }
static void rec_point(Rec *r, int x, int y) { if (x == r->px && y == r->py) return; rec_flush(r); fprintf(r->o, "MouseMove %d %d 300\n", x, y); r->px = x; r->py = y; }

/* one read() of your keyboard -> events; returns 1 on the stop key */
static int rec_feed(Rec *r, const unsigned char *b, int n) {
  static const struct { const char *seq, *name; } K[] = {{"[A", "Up"}, {"[B", "Down"}, {"[C", "Right"}, {"[D", "Left"}, {"OA", "Up"}, {"OB", "Down"}, {"OC", "Right"}, {"OD", "Left"},
    {"[H", "Home"}, {"[F", "End"}, {"OH", "Home"}, {"OF", "End"}, {"[1~", "Home"}, {"[4~", "End"}, {"[2~", "IC"}, {"[3~", "DC"}, {"[5~", "PPage"}, {"[6~", "NPage"},
    {"OP", "F1"}, {"OQ", "F2"}, {"OR", "F3"}, {"OS", "F4"}, {"[15~", "F5"}, {"[17~", "F6"}, {"[18~", "F7"}, {"[19~", "F8"}, {"[20~", "F9"}, {"[21~", "F10"}, {"[Z", "BTab"}, {0}};
  for (int i = 0; i < n; i++) {
    unsigned char c = b[i];
    if (c == 0x1d) return 1;
    if (c == 0x1b) {
      if (i + 1 >= n) { rec_key(r, "Escape"); continue; }
      if (b[i + 1] == '[' && i + 2 < n && b[i + 2] == '<') { /* SGR mouse: ESC [ < b ; x ; y M|m */
        int bt, x, y, used = 0; char fin = 0;
        if (sscanf((const char *)b + i + 3, "%d;%d;%d%c%n", &bt, &x, &y, &fin, &used) >= 4 && (fin == 'M' || fin == 'm')) {
          i += 3 + used - 1;
          if (bt & 32) continue;                                    /* motion: only the click position matters */
          if (bt == 64 || bt == 65) { rec_point(r, x - 1, y - 1); rec_flush(r); fprintf(r->o, "MouseScroll %s\n", bt == 64 ? "up" : "down"); }
          else if (fin == 'M' && (bt & 3) == 0) { rec_point(r, x - 1, y - 1); rec_flush(r); fputs("MouseClick\n", r->o); }
          continue;
        }
      }
      int hit = 0;
      for (int k = 0; K[k].seq; k++) { size_t l = strlen(K[k].seq); if (i + 1 + (int)l <= n && !memcmp(b + i + 1, K[k].seq, l)) { rec_key(r, K[k].name); i += (int)l; hit = 1; break; } }
      if (hit) continue;
      if (b[i + 1] == '[' || b[i + 1] == 'O') { int j = i + 2; while (j < n && !(b[j] >= 0x40 && b[j] <= 0x7e)) j++; i = j; continue; }   /* a sequence nobody mapped: dropped */
      char m[8]; snprintf(m, sizeof m, "M-%c", b[i + 1]); rec_key(r, m); i++; continue;
    }
    if (c == '\r' || c == '\n') { rec_char(r, '\n'); continue; }
    if (c == '\t') { rec_key(r, "Tab"); continue; }
    if (c == 0x7f) { rec_key(r, "BSpace"); continue; }
    if (c < 0x20) { char m[8]; snprintf(m, sizeof m, "C-%c", 'a' + c - 1); rec_key(r, m); continue; }
    rec_char(r, c);
  }
  return 0;
}

static struct termios rec_saved; static int rec_raw;
static void rec_restore(void) { if (rec_raw) { tcsetattr(0, TCSANOW, &rec_saved); rec_raw = 0; } }

static int rec_cmd(int argc, char **argv) {
  const char *outp = argv[2]; int mouse = 0, force = 0, cols = 0, rows = 0, i = 3; char *setup[8]; int ns = 0;
  for (; i < argc; i++) {
    if (!strcmp(argv[i], "--mouse")) mouse = 1; else if (!strcmp(argv[i], "--force")) force = 1;
    else if (!strcmp(argv[i], "-s") && i + 1 < argc && ns < 8) setup[ns++] = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &cols, &rows) != 2 || cols < 20 || rows < 5) { say(LV_ERR, "--size COLSxROWS\n"); return 1; } }
    else if (!strcmp(argv[i], "--")) { i++; break; } else break;
  }
  if (!force && !access(outp, F_OK)) { say(LV_ERR, "%s exists (rec never overwrites; --force does)\n", outp); return 1; }
  if (!isatty(0) || !isatty(1)) { say(LV_ERR, "rec needs a terminal: you play the scenario by hand\n"); return 1; }
  if (!tmux_present()) return 1;
  struct winsize ws; if (ioctl(0, TIOCGWINSZ, &ws)) { say(LV_ERR, "cannot read the terminal size\n"); return 1; }
  if (!cols) { cols = ws.ws_col > 120 ? 120 : ws.ws_col; rows = ws.ws_row > 40 ? 40 : ws.ws_row; }
  if (cols > ws.ws_col || rows > ws.ws_row) { say(LV_ERR, "the demo is %dx%d but this terminal is %dx%d: enlarge it or use --size\n", cols, rows, ws.ws_col, ws.ws_row); return 1; }
  char cmd[2048] = ""; size_t cl = 0;
  if (i >= argc) snprintf(cmd, sizeof cmd, "env 'PS1=$ ' bash --norc -i");
  else for (; i < argc; i++) { int q = strpbrk(argv[i], " \t'\"$`\\;&|<>()*?") != NULL; cl += (size_t)snprintf(cmd + cl, sizeof cmd - cl, "%s%s%s%s", cl ? " " : "", q ? "'" : "", argv[i], q ? "'" : ""); if (cl >= sizeof cmd - 8) { say(LV_ERR, "command too long\n"); return 1; } }
  if (strchr(cmd, '\'') && i >= argc) { /* quoted pieces are fine; a lone ' would break `sh -c` below */ }

  snprintf(tm_sock, sizeof tm_sock, "autodemo-rec-%d", getpid());
  snprintf(tm_home, sizeof tm_home, "/tmp/autodemo-home-XXXXXX");
  if (!mkdtemp(tm_home)) { perror("mkdtemp"); return 1; }
  atexit(home_cleanup);
  snprintf(tm_conf, sizeof tm_conf, "%s/tmux.conf", tm_home);
  FILE *cf = fopen(tm_conf, "w"); if (!cf) { perror(tm_conf); return 1; }
  fprintf(cf, "set -g status off\nset -g remain-on-exit off\nset -g escape-time 0\nset -g default-terminal xterm-256color\nset -g history-limit 2000\nset -g mouse %s\n", mouse ? "on" : "off");
  fclose(cf);
  char xdg[160];
  setenv("HOME", tm_home, 1); unsetenv("TMUX"); setenv("LANG", "C.UTF-8", 1); setenv("LC_ALL", "C.UTF-8", 1); setenv("TERM", "xterm-256color", 1);
  snprintf(xdg, sizeof xdg, "%s/.config", tm_home); setenv("XDG_CONFIG_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/share", tm_home); setenv("XDG_DATA_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.local/state", tm_home); setenv("XDG_STATE_HOME", xdg, 1);
  snprintf(xdg, sizeof xdg, "%s/.cache", tm_home); setenv("XDG_CACHE_HOME", xdg, 1);
  setenv("PS1", "$ ", 1); setenv("COLORTERM", "truecolor", 1);
  for (int s = 0; s < ns; s++) if (system(setup[s])) { say(LV_ERR, "setup failed: %s\n", setup[s]); return 1; }

  Rec r = {0}; r.px = r.py = -1; char *buf = NULL; size_t blen = 0; r.o = open_memstream(&buf, &blen);
  fprintf(r.o, "# recorded by hand with `autodemo rec`; edit freely, then `autodemo render` it\nSize %dx%d\nTyping human\n", cols, rows);
  for (int s = 0; s < ns; s++) fprintf(r.o, "Setup %s\n", setup[s]);
  fprintf(r.o, "Run %s\n", cmd);

  struct winsize pw = {(unsigned short)rows, (unsigned short)cols, 0, 0}; int mfd; char full[2300];
  snprintf(full, sizeof full, "exec %s", cmd);
  pid_t pid = forkpty(&mfd, NULL, NULL, &pw);
  if (pid < 0) { perror("forkpty"); return 1; }
  if (!pid) { execlp("tmux", "tmux", "-L", tm_sock, "-f", tm_conf, "new-session", "-s", "d", "sh", "-c", full, (char *)NULL); _exit(127); }
  say(LV_NOTE, "recording %dx%d: play the scenario, Ctrl+] stops%s\n", cols, rows, mouse ? " (mouse on)" : "");
  usleep(700000);
  tcgetattr(0, &rec_saved); struct termios raw = rec_saved; cfmakeraw(&raw); tcsetattr(0, TCSANOW, &raw); rec_raw = 1; atexit(rec_restore);
  long start = now_ms(), last = start; int stop = 0, nkeys_in = 0;
  struct pollfd pf[2] = {{0, POLLIN, 0}, {mfd, POLLIN, 0}};
  while (!stop) {
    if (poll(pf, 2, 200) < 0) { if (errno == EINTR) continue; break; }
    if (pf[1].revents & POLLIN) { char o[4096]; ssize_t k = read(mfd, o, sizeof o); if (k <= 0) break; (void)!write(1, o, (size_t)k); }
    else if (pf[1].revents & (POLLHUP | POLLERR)) break;
    if (pf[0].revents & POLLIN) {
      unsigned char in[256]; ssize_t k = read(0, in, sizeof in); if (k <= 0) break;
      long t = now_ms(); if (t - last >= 700) rec_sleep(&r, t - last); last = t;
      stop = rec_feed(&r, in, (int)k); nkeys_in += (int)k;
      if (!stop) (void)!write(mfd, in, (size_t)k);
    }
  }
  rec_restore();
  long tail = now_ms() - last; if (tail >= 500) rec_sleep(&r, tail > 5000 ? 5000 : tail);
  rec_flush(&r); fclose(r.o);
  close(mfd);
  (void)run_self("/usr/bin/env", (char *[]){"env", "tmux", "-L", tm_sock, "kill-server", NULL}); waitpid(pid, NULL, 0);
  fputs("\r\n", stdout);
  if (!nkeys_in) { say(LV_ERR, "nothing was typed: no file written\n"); free(buf); return 1; }
  FILE *f = fopen(outp, "w"); if (!f || fwrite(buf, 1, blen, f) != blen) { perror(outp); return 1; } fclose(f); free(buf);
  say(LV_OK, "wrote %s (%.1fs of your time); edit it, then: autodemo check %s && autodemo render %s\n", outp, (now_ms() - start) / 1000.0, outp, outp);
  return 0;
}
