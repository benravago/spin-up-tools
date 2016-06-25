
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/wait.h>

struct toy_context {

  char *name;
  char **optargs;
  int optc;
  unsigned optflags;

  short signal;
  int signalfd;

};

#define GLOBALS(...) struct tt_context { __VA_ARGS__ };

#define error_msg perror
#define error_exit perror_exit
extern void perror_exit(char*, ...);

extern ssize_t readall(int, void*, size_t);
extern ssize_t writeall(int, void*, size_t);

extern void xwrite(int, void*, size_t);
extern void xclose(int);

extern int xsocket(int, int, int);
extern int xconnect(char*, char*, int, int, int, int);

extern int terminal_size(unsigned*, unsigned*);

extern void generic_signal(int);

extern void *xzalloc(size_t);

#if defined(FOR_telnet) || defined(FOR_telnetd)

struct toy_context toys;
struct tt_context TT;
char toybuf[4096];

#endif

#if defined(FOR_telnetd)

#define  FLAG_l 0x0001 //  -l LOGIN  Exec LOGIN on connect                           
#define  FLAG_f 0x0002 //  -f ISSUE_FILE Display ISSUE_FILE instead of /etc/issue    
#define  FLAG_p 0x0004 //  -p PORT Port to listen on                               
#define  FLAG_b 0x0008 //  -b ADDR[:PORT]  Address to bind to                        
#define  FLAG_w 0x0010 //  -w SEC Inetd 'wait' mode, linger time SEC              
#define  FLAG_i 0x0020 //  -i Inetd mode                                             
#define  FLAG_K 0x0040 //  -K Close connection as soon as login exits                
#define  FLAG_F 0x0080 //  -F Run in foreground                                      
#define  FLAG_S 0x0100 //  -S Log to syslog (implied by -i or without -F and -w)     

#endif

