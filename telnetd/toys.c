
#include "toys.h"

struct toy_context toys;

char toybuf[4096];

void xexit() {
  _exit(errno);
}

void generic_signal(int sig)
{
  if (toys.signalfd) {
    char c = sig;

    writeall(toys.signalfd, &c, 1);
  }
  toys.signal = sig;
}

void verror_msg(char *msg, int err, va_list va)
{
  char *s = ": %s";

  fprintf(stderr, "%s: ", toys.name);
  if (msg) vfprintf(stderr, msg, va);
  else s+=2;
  if (err) fprintf(stderr, s, strerror(err));
  if (msg || err) putc('\n', stderr);
}

void perror_exit(char *msg, ...)
{
  va_list va;

  va_start(va, msg);
  verror_msg(msg, errno, va);
  va_end(va);

  xexit();
}

ssize_t readall(int fd, void *buf, size_t len)
{
  size_t count = 0;

  while (count<len) {
    int i = read(fd, (char *)buf+count, len-count);
    if (!i) break;
    if (i<0) return i;
    count += i;
  }

  return count;
}

ssize_t writeall(int fd, void *buf, size_t len)
{
  size_t count = 0;
  while (count<len) {
    int i = write(fd, count+(char *)buf, len-count);
    if (i<1) return i;
    count += i;
  }

  return count;
}

void xwrite(int fd, void *buf, size_t len)
{
  if (len != writeall(fd, buf, len)) perror_exit("xwrite");
}

void xclose(int fd)
{
  if (close(fd)) perror_exit("xclose");
}

int xsocket(int domain, int type, int protocol)
{
  int fd = socket(domain, type, protocol);

  if (fd < 0) perror_exit("socket %x %x", type, protocol);
  return fd;
}

void xsetsockopt(int fd, int level, int opt, void *val, socklen_t len)
{
  if (-1 == setsockopt(fd, level, opt, val, len)) perror_exit("setsockopt");
}

int xconnect(char *host, char *port, int family, int socktype, int protocol, int flags)
{
  struct addrinfo info, *ai, *ai2;
  int fd;

  memset(&info, 0, sizeof(struct addrinfo));
  info.ai_family = family;
  info.ai_socktype = socktype;
  info.ai_protocol = protocol;
  info.ai_flags = flags;

  fd = getaddrinfo(host, port, &info, &ai);
  if (fd || !ai)
    error_exit("Connect '%s%s%s': %s", host, port ? ":" : "", port ? port : "",
      fd ? gai_strerror(fd) : "not found");

  // Try all the returned addresses. Report errors if last entry can't connect.
  for (ai2 = ai; ai; ai = ai->ai_next) {
    fd = (ai->ai_next ? socket : xsocket)(ai->ai_family, ai->ai_socktype,
      ai->ai_protocol);
    if (!connect(fd, ai->ai_addr, ai->ai_addrlen)) break;
    else if (!ai2->ai_next) perror_exit("connect");
    close(fd);
  }
  freeaddrinfo(ai2);

  return fd;
}

int terminal_size(unsigned *xx, unsigned *yy)
{
  struct winsize ws;
  unsigned i, x = 0, y = 0;
  char *s;

  // stdin, stdout, stderr
  for (i=0; i<3; i++) {
    memset(&ws, 0, sizeof(ws));
    if (!ioctl(i, TIOCGWINSZ, &ws)) {
      if (ws.ws_col) x = ws.ws_col;
      if (ws.ws_row) y = ws.ws_row;

      break;
    }
  }
  s = getenv("COLUMNS");
  if (s) sscanf(s, "%u", &x);
  s = getenv("LINES");
  if (s) sscanf(s, "%u", &y);

  // Never return 0 for either value, leave it at default instead.
  if (xx && x) *xx = x;
  if (yy && y) *yy = y;

  return x || y;
}

void *xmalloc(size_t size)
{
  void *ret = malloc(size);
  if (!ret) error_exit("xmalloc(%ld)", (long)size);

  return ret;
}

void *xzalloc(size_t size)
{
  void *ret = xmalloc(size);
  memset(ret, 0, size);
  return ret;
}

