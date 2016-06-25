
#include "toys.h"

extern struct toy_context toys;
extern void telnet_main();

int main (int argc, char **argv ) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr,"\n usage: %s HOST [PORT] \n\n",argv[0]);
    exit(1);    
  }
  toys.name = argv[0];
  toys.optc = --argc;
  toys.optargs = ++argv;
  telnet_main();
}

