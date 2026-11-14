/* sandbox: run a command with a private view of the system, no helper program.
 * user+mount namespace, tmpfs root, system dirs (/usr /etc /lib ...) read-only, only WORKDIR writable, real $HOME invisible
 * (no ~/.ssh, no secrets), optional empty network namespace. Every step fails closed: if one cannot be done, nothing runs. */
#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/stat.h>

static int sb_write(const char *path, const char *s) { int fd = open(path, O_WRONLY); if (fd < 0) return -1; int r = write(fd, s, strlen(s)) == (ssize_t)strlen(s) ? 0 : -1; close(fd); return r; }
static int sb_bind(const char *src, const char *dst, int rw) {
  struct stat st; if (stat(src, &st)) return 0;                      /* missing on this distro: skip */
  if (S_ISDIR(st.st_mode)) { if (mkdir(dst, 0755) && errno != EEXIST) return -1; } else { int fd = open(dst, O_WRONLY | O_CREAT, 0644); if (fd < 0) return -1; close(fd); }
  if (mount(src, dst, NULL, MS_BIND | MS_REC, NULL)) return -1;
  if (!rw) {
    struct statvfs sv; unsigned long fl = MS_BIND | MS_REMOUNT | MS_RDONLY;
    if (!statvfs(src, &sv)) { if (sv.f_flag & ST_NOSUID) fl |= MS_NOSUID; if (sv.f_flag & ST_NODEV) fl |= MS_NODEV; if (sv.f_flag & ST_NOEXEC) fl |= MS_NOEXEC; }
    if (mount(NULL, dst, NULL, fl, NULL)) return -1;
  }
  return 0;
}
/* returns the child's exit status, or -1 when the sandbox could not be set up (nothing was run) */
static void sb_mkpath(const char *root, const char *path) { /* mkdir -p root+path */
  char t[1700]; snprintf(t, sizeof t, "%s%s", root, path); for (char *p = t + strlen(root) + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; } mkdir(t, 0755);
}
static int sandbox_run(const char *work, int net, const char *cmd, const char *ro_extra) {
  int ready[2]; if (pipe(ready)) return -1;
  pid_t pid = fork(); if (pid < 0) return -1;
  if (!pid) {
    close(ready[0]);
    uid_t u = getuid(); gid_t g = getgid(); char map[64];
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | (net ? 0 : CLONE_NEWNET))) goto fail;
    if (sb_write("/proc/self/setgroups", "deny")) goto fail;
    snprintf(map, sizeof map, "%u %u 1", u, u); if (sb_write("/proc/self/uid_map", map)) goto fail;
    snprintf(map, sizeof map, "%u %u 1", g, g); if (sb_write("/proc/self/gid_map", map)) goto fail;
    { pid_t c = fork(); if (c < 0) goto fail;                         /* the child is pid 1 of the new pid namespace: it cannot see or signal host processes */
      if (c) { int st; waitpid(c, &st, 0); _exit(WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)); } }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) goto fail;
    char root[] = "/tmp/autodemo-root-XXXXXX"; if (!mkdtemp(root)) goto fail;
    if (mount("tmpfs", root, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755")) goto fail;
    static const char *ro[] = {"usr", "etc", "bin", "sbin", "lib", "lib64", "opt", NULL}; char s[64], d[512];
    for (int i = 0; ro[i]; i++) {
      snprintf(s, sizeof s, "/%s", ro[i]); snprintf(d, sizeof d, "%s/%s", root, ro[i]);
      struct stat st; if (lstat(s, &st)) continue;
      if (S_ISLNK(st.st_mode)) { char tg[256]; ssize_t n = readlink(s, tg, sizeof tg - 1); if (n < 0) goto fail; tg[n] = 0; if (symlink(tg, d)) goto fail; }
      else if (sb_bind(s, d, 0)) goto fail;
    }
    snprintf(d, sizeof d, "%s/dev", root); if (sb_bind("/dev", d, 1)) goto fail;
    snprintf(d, sizeof d, "%s/proc", root); if (mkdir(d, 0555) || mount("proc", d, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)) goto fail;
    snprintf(d, sizeof d, "%s/tmp", root); if (mkdir(d, 01777) || mount("tmpfs", d, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777,size=512m")) goto fail;
    char w[1024]; if (!realpath(work, w)) goto fail;
    char wd[1600]; snprintf(wd, sizeof wd, "%s%s", root, w); sb_mkpath(root, w);
    if (ro_extra) { char r[1024], rd[1700]; if (!realpath(ro_extra, r)) goto fail; snprintf(rd, sizeof rd, "%s%s", root, r); sb_mkpath(root, r); if (sb_bind(r, rd, 0)) goto fail; }
    if (sb_bind(w, wd, 1)) goto fail;
    snprintf(d, sizeof d, "%s/.old", root); if (mkdir(d, 0700)) goto fail;
    if (syscall(SYS_pivot_root, root, d)) goto fail;
    if (chdir("/") || umount2("/.old", MNT_DETACH) || rmdir("/.old")) goto fail;
    if (chdir(w)) goto fail;
    if (write(ready[1], "1", 1) != 1) goto fail;
    close(ready[1]);
    char home[1100]; snprintf(home, sizeof home, "%s/home", w); mkdir(home, 0755);
    setenv("HOME", home, 1); setenv("TMPDIR", "/tmp", 1); setenv("XDG_CACHE_HOME", home, 1); unsetenv("SSH_AUTH_SOCK"); unsetenv("GPG_AGENT_INFO");
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); _exit(127);
  fail:
    { int e = errno; if (write(ready[1], "0", 1) < 0) {} fprintf(stderr, "sandbox: setup failed (%s); nothing was run\n", strerror(e)); _exit(125); }
  }
  close(ready[1]); char ok = 0; if (read(ready[0], &ok, 1) != 1) ok = 0; close(ready[0]);
  int st; waitpid(pid, &st, 0);
  if (ok != '1') return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}
