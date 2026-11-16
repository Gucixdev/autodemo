/* gui backend: a tiny built-in Wayland compositor (software, headless).
 * The program under test connects to our own socket, draws into wl_shm buffers, we composite them into g->page and
 * inject keyboard/pointer events ourselves. No libwayland, no external tools, works on any desktop (Wayland or Xorg).
 * Window model: toplevels (the first one fills the page, later ones are dialogs centred on it), xdg_popups (menus, combo
 * boxes, tooltips, placed by their xdg_positioner), subsurfaces (any depth, place_above/below), input regions, window
 * geometry. Keyboard focus: topmost grabbing popup, else topmost toplevel. Limits: shm rendering only (set
 * GSK_RENDERER=cairo, LIBGL_ALWAYS_SOFTWARE=1), one client, no XWayland content fix, US key layout. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <poll.h>
#include <signal.h>

enum { T_NONE, T_DISPLAY, T_REGISTRY, T_CALLBACK, T_COMPOSITOR, T_SURFACE, T_REGION, T_SHM, T_POOL, T_BUFFER, T_WMBASE, T_XSURF,
       T_TOPLEVEL, T_SEAT, T_POINTER, T_KEYBOARD, T_OUTPUT, T_SUBCOMP, T_SUBSURF, T_NOP, T_POSNR, T_POPUP };
#define MAXO 1024
#define MAXS 128
#define NKIDS 32
typedef struct { uint8_t type; int a; } Obj;                       /* a: index into surfs/pools/bufs/regions/positioners, or surface index for xsurf/toplevel/popup */
typedef struct { int n; struct { int add, x, y, w, h; } r[16]; } Rgn;
typedef struct { int w, h, ax, ay, aw, ah, anchor, grav, adj, ox, oy; } Posnr;
typedef struct {
  int used; uint32_t obj; int buf, parent, x, y, mapped, top, w, h; Img img; uint32_t cb[16]; int ncb; uint32_t xobj, tobj, pobj;
  int pop, pparent, tparent, seq, gx, gy, gw, gh, tx, ty, px, py, pw, ph, grab, acted;     /* role state: popup / toplevel / window geometry / page origin */
  int nk, kids[NKIDS];                                                                      /* subsurface z-order, -1 marks this surface itself */
  Rgn inp, pinp; int hasinp, phasinp;                                                       /* input region (applied on commit); none = whole surface */
  char title[128];
} Surf;
typedef struct { int used, fd; uint8_t *base; size_t size; int refs, dead; } Pool;   /* refs: buffers cut from it; dead: wl_shm_pool.destroy came, the mapping lives until the last buffer is gone */
typedef struct { int used, pool, off, w, h, stride, fmt; uint32_t obj; } Buf;
typedef struct { int fd, nfd, fds[16]; unsigned char in[1 << 16]; size_t nin; Obj o[MAXO]; Surf s[MAXS]; Pool pool[32]; Buf buf[128]; Rgn rg[32]; int rgused[32]; Posnr pos[32]; int posused[32];
  int nkb, nptr, seatv, entered, kfocus, nseq, acttop; uint32_t kbo[8], ptro[8], ptrsurf, kfs; double pgx, pgy; } Cli;
/* a toolkit may bind wl_seat more than once (GTK and Gecko both do): every pointer/keyboard object gets every event */
#define KSND(c, ...) for (int _k = 0; _k < (c)->nkb; _k++) snd(c, (c)->kbo[_k], __VA_ARGS__)
#define PSND(c, ...) for (int _k = 0; _k < (c)->nptr; _k++) snd(c, (c)->ptro[_k], __VA_ARGS__)
static struct { int lfd, run; char dir[200], sock[64]; pid_t pid, xpid; int xdisp; Cli c; int have, serial, mods; long t0; } W;

/* ---- wire ---- */
static long wl_now(void);
static int wl_trace(void) { static int t = -1; if (t < 0) t = getenv("AUTODEMO_WL_TRACE") != NULL; return t; }
static void snd(Cli *c, uint32_t id, int op, const char *f, ...) {
  unsigned char b[2048]; uint32_t *w = (uint32_t *)b; int n = 2; va_list ap; va_start(ap, f);
  for (; *f; f++) {
    if (*f == 'u' || *f == 'o') w[n++] = va_arg(ap, uint32_t);
    else if (*f == 'i') w[n++] = (uint32_t)va_arg(ap, int);
    else if (*f == 'f') w[n++] = (uint32_t)(int32_t)(va_arg(ap, double) * 256);
    else if (*f == 's') { const char *s = va_arg(ap, const char *); uint32_t l = (uint32_t)strlen(s) + 1; w[n++] = l; memset(&w[n + (l - 1) / 4], 0, 4); memcpy(&w[n], s, l); n += (l + 3) / 4; }
    else if (*f == 'a') { const void *p = va_arg(ap, const void *); uint32_t l = va_arg(ap, uint32_t); w[n++] = l; if (l) { memset(&w[n + (l - 1) / 4], 0, 4); memcpy(&w[n], p, l); } n += (l + 3) / 4; }
  }
  va_end(ap); if (wl_trace()) fprintf(stderr, "wl> %ld obj %u op %d (%d words)\n", wl_now() - W.t0, id, op, n);
  w[0] = id; w[1] = ((uint32_t)n * 4 << 16) | (uint32_t)op;
  size_t len = (size_t)n * 4, off = 0;
  while (off < len) { ssize_t r = write(c->fd, b + off, len - off); if (r > 0) off += r; else if (r < 0 && errno == EAGAIN) usleep(500); else return; }
}
static void snd_fd(Cli *c, uint32_t id, int op, uint32_t fmt, int fd, uint32_t size) { /* wl_keyboard.keymap: one fd in SCM_RIGHTS */
  uint32_t w[5] = {id, (20u << 16) | (uint32_t)op, fmt, size, 0}; w[4] = size;
  struct iovec iov = {w, 16}; char cb[CMSG_SPACE(sizeof(int))] = {0};
  struct msghdr m = {0}; m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
  struct cmsghdr *h = CMSG_FIRSTHDR(&m); h->cmsg_level = SOL_SOCKET; h->cmsg_type = SCM_RIGHTS; h->cmsg_len = CMSG_LEN(sizeof(int)); memcpy(CMSG_DATA(h), &fd, sizeof fd);
  iov.iov_len = 16; w[1] = (16u << 16) | (uint32_t)op; sendmsg(c->fd, &m, 0);
}
static uint32_t rd(const unsigned char **p) { uint32_t v; memcpy(&v, *p, 4); *p += 4; return v; }
static const char *rds(const unsigned char **p) { uint32_t l = rd(p); const char *s = (const char *)*p; *p += (l + 3) & ~3u; return s; }
static void pool_drop(Pool *p) { if (p->base) munmap(p->base, p->size); if (p->fd >= 0) close(p->fd); *p = (Pool){0}; }
static void del_id(Cli *c, uint32_t id) { snd(c, 1, 1, "u", id); if (id < MAXO) c->o[id].type = T_NONE; }
static long wl_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000L + t.tv_nsec / 1000000; }

/* ---- globals ---- */
static const struct { const char *name; int ver; int type; } GL[] = {{"wl_compositor", 4, T_COMPOSITOR}, {"wl_shm", 1, T_SHM}, {"xdg_wm_base", 2, T_WMBASE}, {"wl_seat", 5, T_SEAT},
  {"wl_output", 2, T_OUTPUT}, {"wl_subcompositor", 1, T_SUBCOMP}, {"wl_data_device_manager", 3, T_NOP}};
static char KEYMAP[512]; static size_t KEYMAPN;   /* xkb text; clients compile it with their own xkeyboard-config, so a layout is just a name */
static void keymap_set(const char *layout) {
  KEYMAPN = (size_t)snprintf(KEYMAP, sizeof KEYMAP, "xkb_keymap { xkb_keycodes { include \"evdev+aliases(qwerty)\" }; xkb_types { include \"complete\" }; xkb_compat { include \"complete\" }; xkb_symbols { include \"pc+%s+inet(evdev)\" }; xkb_geometry { include \"pc(pc105)\" }; };\n", layout && *layout ? layout : "us") + 1;
}

/* ---- window model: positions, stacking, hit test ---- */
static int rgn_has(const Rgn *r, int x, int y) { int in = 0; for (int i = 0; i < r->n; i++) if (x >= r->r[i].x && y >= r->r[i].y && x < r->r[i].x + r->r[i].w && y < r->r[i].y + r->r[i].h) in = r->r[i].add; return in; }
/* page position of the surface's top-left corner */
static void spos(Cli *c, int si, int *x, int *y, int d) {
  Surf *s = &c->s[si]; *x = *y = 0; if (d > 8) return;
  if (s->parent >= 0 && c->s[s->parent].used) { spos(c, s->parent, x, y, d + 1); *x += s->x; *y += s->y; }                        /* subsurface: relative to the parent surface */
  else if (s->pop && s->pparent >= 0 && c->s[s->pparent].used) { spos(c, s->pparent, x, y, d + 1); *x += c->s[s->pparent].gx + s->px - s->gx; *y += c->s[s->pparent].gy + s->py - s->gy; } /* popup: relative to the parent's window geometry */
  else { *x = s->tx - s->gx; *y = s->ty - s->gy; }
}
static int tree(Cli *c, int si, int *ord, int n, int d) {
  Surf *s = &c->s[si]; if (d > 6) return n;
  for (int k = 0; k < s->nk; k++) { int e = s->kids[k]; if (e < 0) { if (n < MAXS) ord[n++] = si; } else if (c->s[e].used) n = tree(c, e, ord, n, d + 1); }
  return n;
}
/* draw order, bottom to top: toplevels by creation, then popups by creation, each with its subsurface tree */
static int build_order(Cli *c, int *ord) {
  int n = 0, roots[MAXS], nr = 0;
  for (int pass = 0; pass < 2; pass++) {
    int first = nr;
    for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].mapped && (pass ? c->s[i].pop : c->s[i].top)) roots[nr++] = i;
    for (int a = first; a < nr; a++) for (int b = a + 1; b < nr; b++) if (c->s[roots[b]].seq < c->s[roots[a]].seq) { int t = roots[a]; roots[a] = roots[b]; roots[b] = t; }
  }
  for (int i = 0; i < nr; i++) n = tree(c, roots[i], ord, n, 0);
  return n;
}
static int hit_test(Cli *c, double x, double y, double *lx, double *ly) {
  int ord[MAXS], n = build_order(c, ord);
  for (int k = n - 1; k >= 0; k--) {
    Surf *s = &c->s[ord[k]]; if (!s->mapped) continue;
    int ox, oy; spos(c, ord[k], &ox, &oy, 0); double rx = x - ox, ry = y - oy;
    if (rx < 0 || ry < 0 || rx >= s->w || ry >= s->h) continue;
    if (s->hasinp && !rgn_has(&s->inp, (int)rx, (int)ry)) continue;      /* e.g. Gecko's content subsurface is input-transparent: GDK owns the pointer */
    *lx = rx; *ly = ry; return ord[k];
  }
  return -1;
}
static int top_surf(Cli *c) { int b = -1; for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].top && c->s[i].mapped && (b < 0 || c->s[i].seq < c->s[b].seq)) b = i; return b; } /* the main window */
static int is_main(Cli *c, int si) { for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].top && c->s[i].seq < c->s[si].seq) return 0; return 1; }
static int root_of(Cli *c, int si) { for (int d = 0; d < 8 && si >= 0 && c->s[si].parent >= 0; d++) si = c->s[si].parent; return si; }
static void kid_add(Cli *c, int par, int si, int at) { /* at: index in the parent's list */
  Surf *p = &c->s[par]; if (p->nk >= NKIDS) return; if (at < 0 || at > p->nk) at = p->nk;
  memmove(p->kids + at + 1, p->kids + at, sizeof(int) * (size_t)(p->nk - at)); p->kids[at] = si; p->nk++;
}
static int kid_idx(Cli *c, int par, int si) { for (int k = 0; k < c->s[par].nk; k++) if (c->s[par].kids[k] == si) return k; return -1; }
static void kid_del(Cli *c, int par, int si) { Surf *p = &c->s[par]; int k = kid_idx(c, par, si); if (k >= 0) { memmove(p->kids + k, p->kids + k + 1, sizeof(int) * (size_t)(p->nk - k - 1)); p->nk--; } }

static void blit_surf(G *g, Surf *s, int ox, int oy) {
  Img *P = &g->page; if (!s->img.p) return;
  for (int y = 0; y < s->img.h; y++) { int py = oy + y; if (py < 0 || py >= P->h) continue;
    for (int x = 0; x < s->img.w; x++) { int px = ox + x; if (px < 0 || px >= P->w) continue;
      uint8_t *d = P->p + ((size_t)py * P->w + px) * 4, *q = s->img.p + ((size_t)y * s->img.w + x) * 4; int a = q[3];
      if (a == 255) memcpy(d, q, 4); else for (int k = 0; k < 3; k++) d[k] = (uint8_t)((q[k] * a + d[k] * (255 - a)) / 255); } }
}
static void wl_compose(G *g) {
  Cli *c = &W.c; Img *P = &g->page; uint32_t bg = THEMES[g->theme].bg; int ord[MAXS], n = build_order(c, ord);
  for (long i = 0; i < (long)P->w * P->h; i++) { uint8_t *d = P->p + i * 4; d[0] = bg >> 16; d[1] = bg >> 8; d[2] = bg; d[3] = 255; }
  for (int k = 0; k < n; k++) { int ox, oy; spos(c, ord[k], &ox, &oy, 0); blit_surf(g, &c->s[ord[k]], ox, oy); }
}

/* ---- focus ---- */
static void tl_configure(G *g, Cli *c, int si, int act) { /* xdg_toplevel.configure + xdg_surface.configure; the main window gets the page size, dialogs choose their own */
  Surf *s = &c->s[si]; if (!s->tobj || !s->xobj) return;
  uint32_t st = 4; int m = is_main(c, si); snd(c, s->tobj, 0, "iia", m ? g->cols : 0, m ? g->rows : 0, &st, act ? 4u : 0u); snd(c, s->xobj, 0, "u", ++W.serial); s->acted = act;
}
static int focus_target(Cli *c) { /* topmost grabbing popup, else topmost toplevel */
  int b = -1; for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].pop && c->s[i].grab && c->s[i].mapped && (b < 0 || c->s[i].seq > c->s[b].seq)) b = i;
  if (b >= 0) return b;
  for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].top && c->s[i].mapped && (b < 0 || c->s[i].seq > c->s[b].seq)) b = i;
  return b;
}
static void kb_refocus(G *g, Cli *c) {
  int t = focus_target(c); uint32_t want = t >= 0 ? c->s[t].obj : 0;
  int at = -1; for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].top && c->s[i].mapped && (at < 0 || c->s[i].seq > c->s[at].seq)) at = i;
  if (at != c->acttop && at >= 0) { int old = c->acttop; c->acttop = at; if (old >= 0 && c->s[old].used && c->s[old].top) tl_configure(g, c, old, 0); tl_configure(g, c, at, 1); }
  if (!c->nkb || want == c->kfs) return;
  if (c->kfs) KSND(c, 2, "uo", ++W.serial, c->kfs);
  c->kfs = want; c->kfocus = want != 0;
  if (want) { KSND(c, 1, "uoa", ++W.serial, want, NULL, 0u); KSND(c, 4, "uuuuu", ++W.serial, (uint32_t)W.mods, 0u, 0u, 0u); }
}
static void ptr_refresh(G *g, Cli *c);

static void surf_commit(G *g, Cli *c, int si) {
  Surf *s = &c->s[si]; int was = s->mapped;
  s->inp = s->pinp; s->hasinp = s->phasinp;
  if (s->buf >= 0) {
    Buf *b = &c->buf[s->buf]; Pool *p = &c->pool[b->pool];
    if (p->base && (size_t)b->off + (size_t)b->stride * b->h <= p->size && (b->fmt == 0 || b->fmt == 1)) {
      if (s->img.w != b->w || s->img.h != b->h) { free(s->img.p); s->img = img_new(b->w, b->h); }
      for (int y = 0; y < b->h; y++) { const uint8_t *r = p->base + b->off + (size_t)y * b->stride; uint8_t *d = s->img.p + (size_t)y * b->w * 4;
        for (int x = 0; x < b->w; x++) { d[x * 4] = r[x * 4 + 2]; d[x * 4 + 1] = r[x * 4 + 1]; d[x * 4 + 2] = r[x * 4]; d[x * 4 + 3] = b->fmt == 0 ? r[x * 4 + 3] : 255; } }
      s->w = b->w; s->h = b->h;
      if (!s->mapped && (s->top || s->pop || s->parent >= 0)) s->mapped = 1;
    }
    snd(c, c->buf[s->buf].obj, 0, ""); /* wl_buffer.release */
    s->buf = -1;
  }
  if (!was && s->mapped && s->top && !is_main(c, si)) {                 /* a dialog: centred over its parent window (else the main window) */
    int par = s->tparent >= 0 && c->s[s->tparent].used && c->s[s->tparent].mapped ? s->tparent : top_surf(c);
    int dw = s->gw ? s->gw : s->w, dh = s->gh ? s->gh : s->h;
    if (par >= 0) { Surf *q = &c->s[par]; int qw = q->gw ? q->gw : q->w, qh = q->gh ? q->gh : q->h; s->tx = q->tx + (qw - dw) / 2; s->ty = q->ty + (qh - dh) / 2; }
    if (s->tx < 0) { s->tx = 0; }
    if (s->ty < 0) { s->ty = 0; }
  }
  if (!was && s->mapped) { kb_refocus(g, c); }
  g->dirty = 1; g->page_new = 1;
}

/* ---- popups ---- */
static void popup_place(G *g, Cli *c, int si, const Posnr *p) { /* xdg_positioner -> position relative to the parent's window geometry, kept inside the page */
  Surf *s = &c->s[si]; int ax = p->ax, ay = p->ay, aw = p->aw, ah = p->ah, an = p->anchor, gr = p->grav;
  #define LEFTISH(v) ((v) == 3 || (v) == 5 || (v) == 6)
  #define RIGHTISH(v) ((v) == 4 || (v) == 7 || (v) == 8)
  #define TOPISH(v) ((v) == 1 || (v) == 5 || (v) == 7)
  #define BOTISH(v) ((v) == 2 || (v) == 6 || (v) == 8)
  int pw = p->w > 0 ? p->w : 1, ph = p->h > 0 ? p->h : 1, par = s->pparent, bx = 0, by = 0, ox, oy;
  if (par >= 0 && c->s[par].used) { spos(c, par, &bx, &by, 0); bx += c->s[par].gx; by += c->s[par].gy; }
  for (int pass = 0; pass < 2; pass++) {                                    /* pass 1: flip the anchor+gravity of an axis that does not fit (when the client allows it) */
    int pxx = LEFTISH(an) ? ax : RIGHTISH(an) ? ax + aw : ax + aw / 2, pyy = TOPISH(an) ? ay : BOTISH(an) ? ay + ah : ay + ah / 2;
    ox = (LEFTISH(gr) ? pxx - pw : RIGHTISH(gr) ? pxx : pxx - pw / 2) + p->ox; oy = (TOPISH(gr) ? pyy - ph : BOTISH(gr) ? pyy : pyy - ph / 2) + p->oy;
    if (pass) break;
    int fx = (p->adj & 4) && (bx + ox < 0 || bx + ox + pw > g->cols), fy = (p->adj & 8) && (by + oy < 0 || by + oy + ph > g->rows);
    if (!fx && !fy) break;
    #define FLIPH(v) ((v) == 3 ? 4 : (v) == 4 ? 3 : (v) == 5 ? 7 : (v) == 7 ? 5 : (v) == 6 ? 8 : (v) == 8 ? 6 : (v))
    #define FLIPV(v) ((v) == 1 ? 2 : (v) == 2 ? 1 : (v) == 5 ? 6 : (v) == 6 ? 5 : (v) == 7 ? 8 : (v) == 8 ? 7 : (v))
    if (fx) { an = FLIPH(an); gr = FLIPH(gr); } if (fy) { an = FLIPV(an); gr = FLIPV(gr); }
  }
  if (p->adj & 1) { if (bx + ox + pw > g->cols) ox = g->cols - pw - bx; if (bx + ox < 0) ox = -bx; }     /* slide */
  if (p->adj & 2) { if (by + oy + ph > g->rows) oy = g->rows - ph - by; if (by + oy < 0) oy = -by; }
  s->px = ox; s->py = oy; s->pw = pw; s->ph = ph;
}
static void popup_configure(Cli *c, int si) { Surf *s = &c->s[si]; snd(c, s->pobj, 0, "iiii", s->px, s->py, s->pw, s->ph); snd(c, s->xobj, 0, "u", ++W.serial); }
static int popups_dismiss(G *g, Cli *c, int hit) { /* a press outside the grabbing popups closes them (newest first) and is not delivered; returns 1 when it dismissed */
  int any = 0, rh = hit >= 0 ? root_of(c, hit) : -1;
  for (;;) {
    int b = -1; for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].pop && c->s[i].grab && c->s[i].mapped && (b < 0 || c->s[i].seq > c->s[b].seq)) b = i;
    if (b < 0 || b == rh) break;
    snd(c, c->s[b].pobj, 1, ""); c->s[b].mapped = 0; c->s[b].grab = 0; any = 1;                 /* xdg_popup.popup_done; hidden at once, the client destroys it */
  }
  if (any) { g->page_new = 1; kb_refocus(g, c); }
  return any;
}

/* ---- requests ---- */
static int new_surf(Cli *c) { for (int i = 0; i < MAXS; i++) if (!c->s[i].used) { memset(&c->s[i], 0, sizeof c->s[i]); c->s[i].used = 1; c->s[i].buf = -1; c->s[i].parent = -1; c->s[i].pparent = -1; c->s[i].tparent = -1; c->s[i].nk = 1; c->s[i].kids[0] = -1; return i; } return -1; }
static void surf_free(G *g, Cli *c, int si) {
  Surf *s = &c->s[si]; if (s->parent >= 0) kid_del(c, s->parent, si);
  for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].parent == si) c->s[i].parent = -1;
  if (c->kfs == s->obj) c->kfs = 0;
  if (c->ptrsurf == s->obj) c->ptrsurf = 0;
  if (c->acttop == si) c->acttop = -1;
  s->used = 0; free(s->img.p); s->img.p = NULL; g->page_new = 1; kb_refocus(g, c);
}
static void dispatch(G *g, Cli *c, uint32_t id, int op, const unsigned char *p, int len) {
  if (wl_trace()) fprintf(stderr, "wl< %ld obj %u type %d op %d len %d\n", wl_now() - W.t0, id, id < MAXO ? c->o[id].type : -1, op, len);
  if (id >= MAXO) return;
  Obj *o = &c->o[id];
  switch (o->type) {
  case T_DISPLAY:
    if (op == 0) { uint32_t cb = rd(&p); snd(c, cb, 0, "u", 0u); snd(c, 1, 1, "u", cb); }              /* sync */
    else if (op == 1) { uint32_t r = rd(&p); c->o[r].type = T_REGISTRY;                                   /* get_registry */
      for (size_t i = 0; i < sizeof GL / sizeof *GL; i++) snd(c, r, 0, "usu", (uint32_t)i + 1, GL[i].name, (uint32_t)GL[i].ver); }
    break;
  case T_REGISTRY:
    if (op == 0) { uint32_t name = rd(&p); const char *in = rds(&p); uint32_t ver = rd(&p), nid = rd(&p);
      if (wl_trace()) fprintf(stderr, "wl: bind %s v%u -> %u%s\n", in, ver, nid, name >= 1 && name <= sizeof GL / sizeof *GL ? "" : " (not offered)");
      if (name < 1 || name > sizeof GL / sizeof *GL || nid >= MAXO) break;
      c->o[nid].type = GL[name - 1].type; c->o[nid].a = (int)ver;
      switch (GL[name - 1].type) {
      case T_SHM: snd(c, nid, 0, "u", 0u); snd(c, nid, 0, "u", 1u); break;
      case T_SEAT: snd(c, nid, 0, "u", 3u); if (ver >= 2) snd(c, nid, 1, "s", "seat0"); c->seatv = (int)ver; break;
      case T_OUTPUT: snd(c, nid, 0, "iiiiissi", 0, 0, 340, 190, 0, "autodemo", "virtual", 0); snd(c, nid, 1, "uiii", 3u, g->cols, g->rows, 60000); if (ver >= 2) { snd(c, nid, 3, "i", 1); snd(c, nid, 2, ""); } break;
      } }
    break;
  case T_COMPOSITOR:
    if (op == 0) { uint32_t n = rd(&p); int si = new_surf(c); if (si < 0 || n >= MAXO) break; c->o[n].type = T_SURFACE; c->o[n].a = si; c->s[si].obj = n; }
    else if (op == 1) { uint32_t n = rd(&p); if (n >= MAXO) break; for (int i = 0; i < 32; i++) if (!c->rgused[i]) { c->rgused[i] = 1; c->rg[i].n = 0; c->o[n].type = T_REGION; c->o[n].a = i; break; } }
    break;
  case T_REGION: {
    Rgn *r = &c->rg[o->a];
    if (op == 0) { c->rgused[o->a] = 0; del_id(c, id); }
    else if ((op == 1 || op == 2) && r->n < 16) { r->r[r->n].add = op == 1; r->r[r->n].x = (int)rd(&p); r->r[r->n].y = (int)rd(&p); r->r[r->n].w = (int)rd(&p); r->r[r->n].h = (int)rd(&p); r->n++; }
    break; }
  case T_SURFACE: {
    Surf *s = &c->s[o->a];
    if (op == 0) { int si = o->a; del_id(c, id); surf_free(g, c, si); }
    else if (op == 1) { uint32_t b = rd(&p); s->buf = (b && b < MAXO && c->o[b].type == T_BUFFER) ? c->o[b].a : -1; if (!b) { s->mapped = 0; g->page_new = 1; kb_refocus(g, c); } }
    else if (op == 3) { uint32_t cb = rd(&p); if (cb < MAXO) c->o[cb].type = T_CALLBACK; if (s->ncb < 16) s->cb[s->ncb++] = cb; }
    else if (op == 5) { uint32_t r = rd(&p); if (r && r < MAXO && c->o[r].type == T_REGION) { s->pinp = c->rg[c->o[r].a]; s->phasinp = 1; } else s->phasinp = 0; }   /* set_input_region */
    else if (op == 6) surf_commit(g, c, o->a);
    break; }
  case T_SHM:
    if (op == 0 && c->nfd) { uint32_t n = rd(&p); int size = (int)rd(&p), fd = c->fds[0]; memmove(c->fds, c->fds + 1, sizeof(int) * (--c->nfd)); if (n >= MAXO) { close(fd); break; }
      for (int i = 0; i < 32; i++) if (!c->pool[i].used) { void *m = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0); c->pool[i] = (Pool){1, fd, m == MAP_FAILED ? NULL : m, (size_t)size}; c->o[n].type = T_POOL; c->o[n].a = i; fd = -1; break; }
      if (fd >= 0) close(fd); }
    break;
  case T_POOL: {
    Pool *pl = &c->pool[o->a];
    if (op == 0) { uint32_t n = rd(&p); int off = (int)rd(&p), w = (int)rd(&p), h = (int)rd(&p), st = (int)rd(&p), fmt = (int)rd(&p);
      if (wl_trace()) fprintf(stderr, "wl: buffer off=%d %dx%d stride=%d fmt=%d\n", off, w, h, st, fmt);
      for (int i = 0; i < 128 && n < MAXO; i++) if (!c->buf[i].used) { c->buf[i] = (Buf){1, o->a, off, w, h, st, fmt, n}; pl->refs++; c->o[n].type = T_BUFFER; c->o[n].a = i; break; } }
    else if (op == 1) { pl->dead = 1; if (pl->refs <= 0) pool_drop(pl); del_id(c, id); }   /* destroy is allowed while buffers still use the memory (Xwayland does it at once) */
    else if (op == 2) { size_t ns = (size_t)rd(&p); if (pl->base) munmap(pl->base, pl->size); void *m = mmap(NULL, ns, PROT_READ, MAP_SHARED, pl->fd, 0); pl->base = m == MAP_FAILED ? NULL : m; pl->size = ns; }
    break; }
  case T_BUFFER: if (op == 0) { Buf *bf = &c->buf[o->a]; Pool *pl = &c->pool[bf->pool]; bf->used = 0; if (pl->used && --pl->refs <= 0 && pl->dead) pool_drop(pl); del_id(c, id); } break;
  case T_WMBASE:
    if (op == 2) { uint32_t n = rd(&p), sf = rd(&p); if (n < MAXO && sf < MAXO && c->o[sf].type == T_SURFACE) { c->o[n].type = T_XSURF; c->o[n].a = c->o[sf].a; c->s[c->o[sf].a].xobj = n; } }
    else if (op == 1) { uint32_t n = rd(&p); if (n >= MAXO) break; c->o[n].type = T_NOP; for (int i = 0; i < 32; i++) if (!c->posused[i]) { c->posused[i] = 1; memset(&c->pos[i], 0, sizeof c->pos[i]); c->o[n].type = T_POSNR; c->o[n].a = i; break; } }
    break;
  case T_POSNR: {
    Posnr *q = &c->pos[o->a];
    if (op == 0) { c->posused[o->a] = 0; del_id(c, id); }
    else if (op == 1) { q->w = (int)rd(&p); q->h = (int)rd(&p); }
    else if (op == 2) { q->ax = (int)rd(&p); q->ay = (int)rd(&p); q->aw = (int)rd(&p); q->ah = (int)rd(&p); }
    else if (op == 3) q->anchor = (int)rd(&p);
    else if (op == 4) q->grav = (int)rd(&p);
    else if (op == 5) q->adj = (int)rd(&p);
    else if (op == 6) { q->ox = (int)rd(&p); q->oy = (int)rd(&p); }
    break; }
  case T_XSURF:
    if (op == 1) { uint32_t n = rd(&p); if (n >= MAXO) break; Surf *s = &c->s[o->a]; c->o[n].type = T_TOPLEVEL; c->o[n].a = o->a; s->top = 1; s->tobj = n; s->seq = ++c->nseq;
      tl_configure(g, c, o->a, is_main(c, o->a)); if (is_main(c, o->a)) c->acttop = o->a; }   /* configure: size (main window only) + activated (dialogs get it when they map) */
    else if (op == 2) { uint32_t n = rd(&p), par = rd(&p), pos = rd(&p); Surf *s = &c->s[o->a];                                   /* get_popup */
      if (n >= MAXO) break;
      c->o[n].type = T_POPUP; c->o[n].a = o->a; s->pop = 1; s->pobj = n; s->seq = ++c->nseq; s->pparent = par < MAXO && c->o[par].type == T_XSURF ? c->o[par].a : top_surf(c);
      if (pos < MAXO && c->o[pos].type == T_POSNR) popup_place(g, c, o->a, &c->pos[c->o[pos].a]);
      popup_configure(c, o->a); }
    else if (op == 3) { Surf *s = &c->s[o->a]; s->gx = (int)rd(&p); s->gy = (int)rd(&p); s->gw = (int)rd(&p); s->gh = (int)rd(&p); g->page_new = 1; }  /* set_window_geometry */
    else if (op == 0) del_id(c, id);
    break;
  case T_TOPLEVEL: {
    Surf *s = &c->s[o->a];
    if (op == 0) { s->top = 0; s->mapped = 0; g->page_new = 1; del_id(c, id); kb_refocus(g, c); }
    else if (op == 1) { uint32_t par = rd(&p); s->tparent = par && par < MAXO && c->o[par].type == T_TOPLEVEL ? c->o[par].a : -1; }
    else if (op == 2) { const char *t = rds(&p); snprintf(s->title, sizeof s->title, "%s", t); }
    break; }
  case T_POPUP: {
    Surf *s = &c->s[o->a];
    if (op == 0) { s->pop = 0; s->grab = 0; s->mapped = 0; g->page_new = 1; del_id(c, id); kb_refocus(g, c); }
    else if (op == 1) s->grab = 1;                                                                     /* grab(seat, serial) */
    else if (op == 2) { uint32_t pos = rd(&p), tok = rd(&p); if (pos < MAXO && c->o[pos].type == T_POSNR) { popup_place(g, c, o->a, &c->pos[c->o[pos].a]); snd(c, id, 2, "u", tok); popup_configure(c, o->a); g->page_new = 1; } }
    break; }
  case T_SEAT:
    if (op == 0) { uint32_t n = rd(&p); if (n < MAXO) { c->o[n].type = T_POINTER; if (c->nptr < 8) c->ptro[c->nptr++] = n; } }
    else if (op == 1) { uint32_t n = rd(&p); if (n >= MAXO) break; c->o[n].type = T_KEYBOARD; if (c->nkb < 8) c->kbo[c->nkb++] = n;
      int fd = memfd_create("keymap", 0); if (fd >= 0) { if (write(fd, KEYMAP, KEYMAPN) > 0) snd_fd(c, n, 0, 1, fd, (uint32_t)KEYMAPN); close(fd); }
      if (o->a >= 4) snd(c, n, 5, "ii", 25, 600);
      if (c->kfs) { snd(c, n, 1, "uoa", ++W.serial, c->kfs, NULL, 0u); snd(c, n, 4, "uuuuu", ++W.serial, (uint32_t)W.mods, 0u, 0u, 0u); } else kb_refocus(g, c); }
    break;
  case T_SUBCOMP:
    if (op == 1) { uint32_t n = rd(&p), sf = rd(&p), par = rd(&p);
      if (n < MAXO && sf < MAXO && par < MAXO && c->o[sf].type == T_SURFACE && c->o[par].type == T_SURFACE) { c->o[n].type = T_SUBSURF; c->o[n].a = c->o[sf].a; c->s[c->o[sf].a].parent = c->o[par].a; kid_add(c, c->o[par].a, c->o[sf].a, -1); } }
    break;
  case T_SUBSURF:
    if (op == 1) { Surf *s = &c->s[o->a]; s->x = (int)rd(&p); s->y = (int)rd(&p); g->page_new = 1; }
    else if (op == 2 || op == 3) {                                                                   /* place_above / place_below a sibling (or the parent itself) */
      Surf *s = &c->s[o->a]; uint32_t sib = rd(&p); int par = s->parent; if (par < 0 || sib >= MAXO || c->o[sib].type != T_SURFACE) break;
      int sa = c->o[sib].a, to = sa == par ? kid_idx(c, par, -1) : kid_idx(c, par, sa); if (to < 0) break;
      kid_del(c, par, o->a); to = sa == par ? kid_idx(c, par, -1) : kid_idx(c, par, sa); kid_add(c, par, o->a, op == 2 ? to + 1 : to); g->page_new = 1; }
    else if (op == 0) { Surf *s = &c->s[o->a]; if (s->parent >= 0) kid_del(c, s->parent, o->a); s->parent = -1; s->mapped = 0; g->page_new = 1; del_id(c, id); }
    break;
  case T_POINTER: case T_KEYBOARD:
    if (op == (o->type == T_POINTER ? 1 : 0)) {
      uint32_t *l = o->type == T_POINTER ? c->ptro : c->kbo; int *n = o->type == T_POINTER ? &c->nptr : &c->nkb;
      for (int i = 0; i < *n; i++) if (l[i] == id) { memmove(l + i, l + i + 1, sizeof *l * (size_t)(*n - i - 1)); (*n)--; break; }
      del_id(c, id); }
    break;
  default: if (op == 0 && (o->type == T_NOP)) { if (len == 0) del_id(c, id); } break;
  }
  (void)len;
}
static void cli_read(G *g, Cli *c) {
  struct iovec iov = {c->in + c->nin, sizeof c->in - c->nin}; char cb[CMSG_SPACE(16 * sizeof(int))]; struct msghdr m = {0};
  m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
  ssize_t r = recvmsg(c->fd, &m, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
  if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) { if (wl_trace()) fprintf(stderr, "wl: client gone (r=%zd)\n", r); close(c->fd); c->fd = -1; return; }
  if (r < 0) return;
  for (struct cmsghdr *h = CMSG_FIRSTHDR(&m); h; h = CMSG_NXTHDR(&m, h)) if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS) {
    int n = (int)((h->cmsg_len - CMSG_LEN(0)) / sizeof(int)); for (int i = 0; i < n && c->nfd < 16; i++) memcpy(&c->fds[c->nfd++], CMSG_DATA(h) + i * sizeof(int), sizeof(int)); }
  c->nin += (size_t)r;
  size_t off = 0;
  while (c->nin - off >= 8) {
    uint32_t id, w; memcpy(&id, c->in + off, 4); memcpy(&w, c->in + off + 4, 4); size_t sz = w >> 16; if (sz < 8 || sz > sizeof c->in) { if (wl_trace()) fprintf(stderr, "wl: bad size %zu\n", sz); close(c->fd); c->fd = -1; return; }
    if (c->nin - off < sz) break;
    dispatch(g, c, id, (int)(w & 0xffff), c->in + off + 8, (int)sz - 8); off += sz;
  }
  memmove(c->in, c->in + off, c->nin - off); c->nin -= off;
}

/* one pump per timeline step: accept, read requests, fire frame callbacks, rebuild the page */
static void wl_pump(G *g) {
  if (W.lfd >= 0 && W.c.fd < 0) { int fd = accept4(W.lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0) { Cli *c = &W.c; memset(c, 0, sizeof *c); c->fd = fd; c->acttop = -1; c->o[1].type = T_DISPLAY; W.have = 1; } }
  Cli *c = &W.c; if (c->fd < 0) return;
  for (int i = 0; i < 8 && c->fd >= 0; i++) { struct pollfd pf = {c->fd, POLLIN, 0}; if (poll(&pf, 1, i ? 0 : 2) <= 0) break; cli_read(g, c); }
  if (c->fd < 0) return;
  long now = wl_now() - W.t0;
  for (int i = 0; i < MAXS; i++) if (c->s[i].used) { for (int k = 0; k < c->s[i].ncb; k++) { snd(c, c->s[i].cb[k], 0, "u", (uint32_t)now); del_id(c, c->s[i].cb[k]); } c->s[i].ncb = 0; }
  if (g->page_new) { g->page_new = 0; wl_compose(g); ptr_refresh(g, c); }
}

/* ---- input ---- */
static void wl_key(Cli *c, int code, int down) { if (c->nkb && c->kfocus) KSND(c, 3, "uuuu", ++W.serial, (uint32_t)(wl_now() - W.t0), (uint32_t)code, (uint32_t)down); }
static void wl_mods(Cli *c, int m) { if (c->nkb && c->kfocus) KSND(c, 4, "uuuuu", ++W.serial, (uint32_t)m, 0u, 0u, 0u); W.mods = m; }
static const char *K1 = "`1234567890-=", *K1S = "~!@#$%^&*()_+", *K2 = "qwertyuiop[]\\", *K2S = "QWERTYUIOP{}|", *K3 = "asdfghjkl;'", *K3S = "ASDFGHJKL:\"", *K4 = "zxcvbnm,./", *K4S = "ZXCVBNM<>?";
static int ch_key(char ch, int *shift) { /* ASCII -> evdev keycode (US layout) */
  static const struct { const char **s; int first; } R[] = {{&K1, 41}, {&K2, 16}, {&K3, 30}, {&K4, 44}}; static const char **S[] = {&K1S, &K2S, &K3S, &K4S};
  static const int kc[4][13] = {{41, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 43}, {30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}, {44, 45, 46, 47, 48, 49, 50, 51, 52, 53}};
  (void)R; if (ch == ' ') { *shift = 0; return 57; } if (ch == '\n') { *shift = 0; return 28; } if (ch == '\t') { *shift = 0; return 15; }
  const char **N[] = {&K1, &K2, &K3, &K4};
  for (int r = 0; r < 4; r++) { const char *p = strchr(*N[r], ch); if (p && ch) { *shift = 0; return kc[r][p - *N[r]]; } p = strchr(*S[r], ch); if (p && ch) { *shift = 1; return kc[r][p - *S[r]]; } }
  return -1;
}
static void gui_tapkey(Cli *c, int code, int mods) { if (mods != W.mods) wl_mods(c, mods); wl_key(c, code, 1); wl_key(c, code, 0); if (mods != W.mods) wl_mods(c, 0); }
static int gui_text(G *g, const char *s, int speed) {
  Cli *c = &W.c;
  for (const char *p = s; *p; p++) {
    int sh = 0, k, mods;
    if ((unsigned char)*p >= 0x80) {   /* Layout pl: Polish letters are AltGr (level3, Mod5 = 0x80) + the base letter, Shift for capitals */
      const unsigned char *q = (const unsigned char *)p; uint32_t cp = utf8_next(&q); p = (const char *)q - 1;
      static const uint32_t L[] = {0x105, 0x107, 0x119, 0x142, 0x144, 0xf3, 0x15b, 0x17a, 0x17c}; static const char B[] = "acelnosxz";
      int cap = 0, idx = -1; for (int i = 0; i < 9; i++) { if (cp == L[i]) idx = i; if (cp == (L[i] == 0xf3 ? 0xd3 : L[i] - 1)) { idx = i; cap = 1; } }
      if (strcmp(g->layout, "pl") || idx < 0) { say(LV_WARN, "gui Send: U+%04X needs 'Layout pl' (and a Polish letter), skipped\n", cp); continue; }
      k = ch_key(B[idx], &sh); mods = 0x80 | (cap ? 1 : 0);
    } else {
      k = ch_key(*p, &sh); mods = sh ? 1 : 0;
      if (k < 0) { say(LV_WARN, "gui Send: character '%c' not in the US layout table, skipped\n", *p); continue; }
    }
    gui_tapkey(c, k, mods); g->idle = 0; if (g->human) tick(g, jitter(speed)); else wl_pump(g);
  }
  tick(g, 40); wait_ms(g, sp(g, CFG_SEND_PAUSE_MS)); return 0;
}
static int gui_key(G *g, const char *spec) {
  static const struct { const char *n; int k; } M[] = {{"Enter", 28}, {"Escape", 1}, {"Tab", 15}, {"BTab", 15}, {"Space", 57}, {"BSpace", 14}, {"Up", 103}, {"Down", 108}, {"Left", 105}, {"Right", 106},
    {"Home", 102}, {"End", 107}, {"PageUp", 104}, {"PgUp", 104}, {"PageDown", 109}, {"PgDn", 109}, {"DC", 111}, {"IC", 110}, {"F1", 59}, {"F2", 60}, {"F3", 61}, {"F4", 62}, {"F5", 63}, {"F6", 64},
    {"F7", 65}, {"F8", 66}, {"F9", 67}, {"F10", 68}, {"F11", 87}, {"F12", 88}, {0}};
  char b[64]; snprintf(b, sizeof b, "%s", spec); int rep = 1; char *star = strchr(b, '*'); if (star) { *star = 0; rep = atoi(star + 1); }
  int mods = 0; const char *k = b; if (b[1] == '-' && b[2]) { mods = b[0] == 'C' ? 4 : b[0] == 'M' ? 8 : 1; k = b + 2; } if (!strcmp(k, "BTab")) mods |= 1;
  int code = -1; for (int i = 0; M[i].n; i++) if (!strcmp(k, M[i].n)) code = M[i].k;
  if (code < 0 && strlen(k) == 1) { int sh; code = ch_key(k[0], &sh); if (sh) mods |= 1; }
  if (code < 0) { say(LV_ERR, "gui: unknown key '%s'\n", spec); return -1; }
  for (int i = 0; i < rep; i++) { gui_tapkey(&W.c, code, mods); g->idle = 0; tick(g, rep > 1 ? 90 : 120); }
  return 0;
}
static void ptr_to(G *g, double x, double y) { /* x,y: page pixels; picks the surface under the pointer (popups first, input regions honoured) and sends leave/enter/motion */
  Cli *c = &W.c; if (!c->nptr) return; c->pgx = x; c->pgy = y; double lx = 0, ly = 0; int hit = hit_test(c, x, y, &lx, &ly);
  if (hit < 0) { if (c->ptrsurf) { PSND(c, 1, "uo", ++W.serial, c->ptrsurf); if (c->seatv >= 5) PSND(c, 5, ""); c->ptrsurf = 0; } return; }
  uint32_t so = c->s[hit].obj;
  if (c->ptrsurf != so) {
    if (c->ptrsurf) PSND(c, 1, "uo", ++W.serial, c->ptrsurf);
    c->ptrsurf = so; PSND(c, 0, "uoff", ++W.serial, so, lx, ly);
  } else PSND(c, 2, "uff", (uint32_t)(wl_now() - W.t0), lx, ly);
  c->entered = 1; if (c->seatv >= 5) PSND(c, 5, ""); (void)g;
}
static void ptr_refresh(G *g, Cli *c) { /* the picture changed under a still pointer (popup opened, window closed): re-pick the focus surface */
  if (!c->nptr || !c->entered) return;
  double lx, ly; int hit = hit_test(c, c->pgx, c->pgy, &lx, &ly);
  if ((hit >= 0 ? c->s[hit].obj : 0u) != c->ptrsurf) ptr_to(g, c->pgx, c->pgy);
}
static void gui_move(G *g, double x, double y, int ms) {
  if (!g->ptr_on) { g->ptr_on = 1; g->px = g->tx0 + x + 60; g->py = g->ty0 + y + 40; }
  double sx = g->px - g->tx0, sy = g->py - g->ty0; int n = ms / 40; if (n < 1) n = 1;
  for (int i = 1; i <= n; i++) {
    double u = (double)i / n; u = u * u * (3 - 2 * u); double cx = sx + (x - sx) * u, cy = sy + (y - sy) * u;
    g->px = g->tx0 + cx; g->py = g->ty0 + cy; ptr_to(g, cx, cy); g->dirty = 1; tick(g, ms / n);
  }
}
static int gui_click(G *g) {
  Cli *c = &W.c; if (!c->nptr) return 0; double x = g->px - g->tx0, y = g->py - g->ty0; ptr_to(g, x, y);
  tick(g, 80); ptr_to(g, x, y); tick(g, 60); /* hover first: toolkits ignore a press that comes right after enter */
  double lx, ly; int hit = hit_test(c, x, y, &lx, &ly), eaten = popups_dismiss(g, c, hit);   /* a press outside the open menus only closes them */
  uint32_t t = (uint32_t)(wl_now() - W.t0);
  if (!eaten) { PSND(c, 3, "uuuu", ++W.serial, t, 272u, 1u); if (c->seatv >= 5) PSND(c, 5, ""); }
  for (int i = 1; i <= 2; i++) { g->ripple = i; g->dirty = 1; tick(g, 50); }
  if (!eaten) { PSND(c, 3, "uuuu", ++W.serial, (uint32_t)(wl_now() - W.t0), 272u, 0u); if (c->seatv >= 5) PSND(c, 5, ""); }
  for (int i = 3; i <= 4; i++) { g->ripple = i; g->dirty = 1; tick(g, 50); }
  g->ripple = 0; g->dirty = 1; tick(g, 120); return 0;
}
static int gui_wheel(G *g, int up, int n) {
  Cli *c = &W.c; if (!c->nptr) return 0; if (!g->ptr_on) gui_move(g, g->cols / 2.0, g->rows / 2.0, 1);
  for (int i = 0; i < n; i++) { PSND(c, 4, "uuf", (uint32_t)(wl_now() - W.t0), 0u, up ? -15.0 : 15.0); if (c->seatv >= 5) PSND(c, 5, ""); g->idle = 0; tick(g, 70); }
  wait_ms(g, sp(g, CFG_SCROLL_PAUSE_MS)); return 0;
}

/* ---- WaitFor (gui): title "text" [timeout] | settle MS [timeout] ---- */
static int gui_title_has(const char *t) { Cli *c = &W.c; for (int i = 0; i < MAXS; i++) if (c->s[i].used && c->s[i].top && c->s[i].mapped && strstr(c->s[i].title, t)) return 1; return 0; }
static int gui_waitfor(G *g, int line, int n, char **a) {
  int to = 30000, quiet = 0, bytitle = 0;
  if (n >= 2 && !strcmp(a[0], "title")) { bytitle = 1; if (n == 3) dur_ms(a[2], &to); }
  else if (n >= 2 && !strcmp(a[0], "settle")) { dur_ms(a[1], &quiet); if (n == 3) dur_ms(a[2], &to); }
  else { say(LV_ERR, "line %d: with Window gui WaitFor is: WaitFor title \"text\" [timeout]  or  WaitFor settle MS [timeout]\n", line); return -1; }
  long t0 = wl_now(), last = t0; uint8_t *prev = NULL; size_t sz = 0; int rl = g->realtime; g->realtime = 1;
  for (;;) {
    tick(g, 50); long now = wl_now(); Img *P = &g->page;
    if (bytitle) { if (gui_title_has(a[1])) break; }
    else if (P->p) {
      if (!prev || sz != (size_t)P->w * P->h * 4 || memcmp(prev, P->p, sz)) { sz = (size_t)P->w * P->h * 4; prev = realloc(prev, sz); memcpy(prev, P->p, sz); last = now; }
      else if (now - last >= quiet) break;
    }
    if (g->dead) { say(LV_ERR, "line %d: WaitFor: the program exited before the window %s\n", line, bytitle ? "title matched" : "settled"); free(prev); g->realtime = rl; return -1; }
    if (now - t0 > to) {
      if (bytitle) say(LV_ERR, "line %d: WaitFor title \"%s\": no window title contains it after %.1fs\n", line, a[1], to / 1000.0);
      else say(LV_ERR, "line %d: WaitFor settle %dms: the window was still changing after %.1fs\n", line, quiet, to / 1000.0);
      free(prev); g->realtime = rl; return -1;
    }
  }
  free(prev); g->realtime = rl; return 0;
}

/* ---- lifecycle ---- */
static int gui_capture(G *g) {
  wl_pump(g);
  if (W.pid > 0 && !g->dead) { int st; if (waitpid(W.pid, &st, WNOHANG) == W.pid) { g->dead = 1; g->dead_status = WIFEXITED(st) ? WEXITSTATUS(st) : 1; W.pid = 0; } }
  Img *P = &g->page; if (!P->p) return 0;
  uint64_t h = 1469598103934665603ULL; for (long i = 0; i < (long)P->w * P->h * 4; i += 61) h = (h ^ P->p[i]) * 1099511628211ULL;
  if (h != g->last_hash) { g->nstates++; g->last_hash = h; g->idle = 0; render_layer(g); g->dirty = 1; } else g->idle++;
  return 0;
}
static void gui_blit(G *g) {
  Img *L = &g->layer, *P = &g->page; if (!P->p) return;
  for (int y = 0; y < P->h && g->ty0 + y < L->h; y++) {
    int skip = 0, fb = P->h - 1 - y;
    if (fb < CFG_CORNER_R) { double dy = CFG_CORNER_R - fb - 0.5; skip = CFG_CORNER_R - (int)sqrt((double)CFG_CORNER_R * CFG_CORNER_R - dy * dy); }
    if (P->w - skip > skip) memcpy(L->p + ((size_t)(g->ty0 + y) * L->w + g->tx0 + skip) * 4, P->p + ((size_t)y * P->w + skip) * 4, (size_t)(P->w - 2 * skip) * 4);
  }
}
static void gui_close(G *g) { (void)g;
  if (W.xpid > 0) { kill(-W.xpid, SIGTERM); usleep(100000); kill(-W.xpid, SIGKILL); waitpid(W.xpid, NULL, 0); W.xpid = 0; }
  if (W.pid > 0) { kill(-W.pid, SIGTERM); for (int i = 0; i < 20; i++) { if (waitpid(W.pid, NULL, WNOHANG) == W.pid) { W.pid = 0; break; } usleep(50000); }
    if (W.pid > 0) { kill(-W.pid, SIGKILL); waitpid(W.pid, NULL, 0); W.pid = 0; } } if (W.lfd >= 0) { close(W.lfd); W.lfd = -1; } if (W.sock[0]) { char p[300]; snprintf(p, sizeof p, "%s/%s", W.dir, W.sock); unlink(p); rmdir(W.dir); W.sock[0] = 0; } }
static int gui_start(G *g, const char *cmd) {
  snprintf(W.dir, sizeof W.dir, "%s/run", tm_home); mkdir(W.dir, 0700); snprintf(W.sock, sizeof W.sock, "wayland-autodemo");
  struct sockaddr_un a = {.sun_family = AF_UNIX}; snprintf(a.sun_path, sizeof a.sun_path, "%s/%s", W.dir, W.sock); unlink(a.sun_path);
  W.lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); W.c.fd = -1;
  if (W.lfd < 0 || bind(W.lfd, (struct sockaddr *)&a, sizeof a) || listen(W.lfd, 4)) { say(LV_ERR, "cannot listen on %s: %s\n", a.sun_path, strerror(errno)); return -1; }
  keymap_set(g->layout); signal(SIGPIPE, SIG_IGN); W.t0 = wl_now(); g->realtime = 1; /* async GUI toolkits need real time between events, idle-skipping would burst them */
  g->page = img_new(g->cols, g->rows); g->have_session = 1; g->in_run = 1;
  if (g->x11) {   /* X11 programs: a rootful Xwayland is just one more Wayland client (shm, one window) of our compositor */
    int pf[2]; if (pipe(pf)) return -1;
    W.xpid = fork(); if (W.xpid < 0) return -1;
    if (!W.xpid) {
      setpgid(0, 0); close(pf[0]); char fdn[16], geo[32]; snprintf(fdn, sizeof fdn, "%d", pf[1]); snprintf(geo, sizeof geo, "%dx%d", g->cols, g->rows);
      setenv("XDG_RUNTIME_DIR", W.dir, 1); setenv("WAYLAND_DISPLAY", W.sock, 1); unsetenv("DISPLAY");
      int dn = open("/dev/null", 2); dup2(dn, 0); if (!verbose) { dup2(dn, 1); dup2(dn, 2); }
      execlp("Xwayland", "Xwayland", "-shm", "-noreset", "-geometry", geo, "-displayfd", fdn, (char *)NULL); _exit(127);
    }
    close(pf[1]); char nb[32] = {0}; int nn = 0, ok = 0;
    for (int waited = 0; waited < 10000 && !ok; waited += 20) {        /* Xwayland needs our answers while it starts, so keep pumping */
      wl_pump(g); struct pollfd pfd = {pf[0], POLLIN, 0};
      if (poll(&pfd, 1, 20) > 0) { ssize_t r = read(pf[0], nb + nn, sizeof nb - 1 - nn); if (r > 0) { nn += (int)r; if (memchr(nb, '\n', nn)) ok = 1; } else if (r == 0) break; }
    }
    close(pf[0]);
    if (!ok) { say(LV_ERR, "Xwayland did not start (is it installed?)\n"); return -1; }
    W.xdisp = atoi(nb);
  }
  W.pid = fork(); if (W.pid < 0) return -1;
  if (!W.pid) {
    setpgid(0, 0);
    setenv("XDG_RUNTIME_DIR", W.dir, 1); setenv("WAYLAND_DISPLAY", W.sock, 1); unsetenv("DISPLAY"); unsetenv("WAYLAND_SOCKET");
    setenv("GDK_BACKEND", "wayland", 1); setenv("QT_QPA_PLATFORM", "wayland", 1); setenv("MOZ_ENABLE_WAYLAND", "1", 1); setenv("MOZ_WEBRENDER", "0", 1); setenv("SDL_VIDEODRIVER", "wayland", 1);
    setenv("GSK_RENDERER", "cairo", 1); setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1); setenv("GDK_DEBUG", "gl-disable", 1);
    if (g->x11) { char dsp[16]; snprintf(dsp, sizeof dsp, ":%d", W.xdisp); setenv("DISPLAY", dsp, 1); unsetenv("WAYLAND_DISPLAY"); unsetenv("MOZ_ENABLE_WAYLAND"); setenv("GDK_BACKEND", "x11", 1); setenv("QT_QPA_PLATFORM", "xcb", 1); setenv("SDL_VIDEODRIVER", "x11", 1); }
    for (int i = 0; i < g->nenv; i++) { char *e = strdup(g->env[i]), *q = strchr(e, '='); if (q) { *q = 0; setenv(e, q + 1, 1); } }
    if (g->cwd[0] && chdir(g->cwd)) _exit(126);
    int dn = open("/dev/null", 2); dup2(dn, 0); if (!verbose) { dup2(dn, 1); dup2(dn, 2); }
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); _exit(127);
  }
  for (int waited = 0; waited < CFG_GUI_WAIT_MS; waited += 50) {
    wl_pump(g); if (top_surf(&W.c) >= 0 && (!g->x11 || waited >= 1500)) { wl_pump(g); msleep(g, CFG_GUI_SETTLE_MS); wl_pump(g); wl_compose(g); return 0; }
    int st; if (waitpid(W.pid, &st, WNOHANG) == W.pid) { W.pid = 0; say(LV_ERR, "the program exited before opening a window (status %d). Needs Wayland shm rendering; try GSK_RENDERER=cairo / a Wayland-capable program\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1); return -1; }
    msleep(g, 50);
  }
  say(LV_ERR, "no window within %d s for: %s\n", CFG_GUI_WAIT_MS / 1000, cmd); return -1;
}
