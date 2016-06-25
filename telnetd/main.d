
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

int main (int argc, char **argv ) {
  toys.optc = argc;
  toys.optargs = argv;
  telnetd_main();
}

