#define FOR_telnetd
#include "toys.h"

GLOBALS(
    char *login_path;
    char *issue_path;
    int port;
    char *host_addr;
    long w_sec;

    int gmax_fd;
    pid_t fork_pid;
)

extern struct tt_context TT;

extern struct toy_context toys;
extern void telnetd_main();

void bind_address(char*);
void usage();

int main (int argc, char **argv ) {

  int c;
  while ((c = getopt(argc, argv, "l:f:p:b:w:iKFS")) != -1) {
    switch (c) {
      case 'l': toys.optflags |= FLAG_l; TT.login_path = optarg; break;
      case 'f': toys.optflags |= FLAG_f; TT.issue_path = optarg; break;
      case 'p': toys.optflags |= FLAG_p; TT.port = atoi(optarg); break;
      case 'b': toys.optflags |= FLAG_b; bind_address(optarg); break;
      case 'w': toys.optflags |= FLAG_w; TT.w_sec = atol(optarg); break;
      case 'i': toys.optflags |= FLAG_i; break;
      case 'K': toys.optflags |= FLAG_K; break;
      case 'F': toys.optflags |= FLAG_F; break;
      case 'S': toys.optflags |= FLAG_S; break;
      default: usage(argv[0]); break;
    }
  }

  if (!TT.host_addr)  { toys.optflags |= FLAG_b; TT.host_addr = "127.0.0.1"; }
  if (!TT.port)       { toys.optflags |= FLAG_p; TT.port = 23; }
  if (!TT.login_path) { toys.optflags |= FLAG_l; TT.login_path = "/bin/login"; }

  toys.optc = argc;
  toys.optargs = argv;
  telnetd_main();
}

void bind_address(char* optarg) {
  TT.host_addr = optarg;
  char *p = strchr(optarg,':');
  if (p) {
    *p = 0;
    TT.port = atoi(++p);
    toys.optflags |= FLAG_p;
  }
}

void usage(char *cmd) {
  fprintf(stderr,"\n usage: %s [-iKFS] [-l login] [-f issue] [-p port] [-b addr[:port]] [-w sec] \n\n",cmd);
  exit(1);
}

