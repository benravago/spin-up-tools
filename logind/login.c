#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/utsname.h>
#include <crypt.h>
#include <ctype.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <utmpx.h>

void timeout(int sig) {
    puts("login timeout\n");
    exit(0);
}

int trim(char* str) {
    int len = strlen(str);
    while (len && isspace(str[len-1])) {
        str[--len] = 0;
    }
    return len;
}

int prompt(char* msg, char* buf, int buflen) {
    memset(buf,0,buflen);
    fputs(msg,stdout);
    fflush(stdout);
    if (fgets(buf,buflen-1,stdin)) {
        return trim(buf);
    }
    return 0;
}

void banner() {
    struct utsname u;
    if (!uname(&u)) {
      printf("%s - %s - %s - %s \n\n",u.nodename,u.sysname,u.release,u.machine);
    }
}

int main(void) {

    char username[ 1 + sizeof(((struct utmpx *)0)->ut_user) ];
    char password[ 2 * sizeof(username) ];

    struct passwd *p;

    signal(SIGALRM,timeout);
    putchar('\n');

    for (int count = 0; count < 3; count++) {

        p = 0;

        alarm(60);
        tcflush(0,TCIFLUSH);

        banner();

        if (!prompt("login: ",username,sizeof(username))) {
            puts("no username\n");
            continue; // retry
        }

        p = getpwnam(username);

        if (!p) {
            printf("user %s not found\n",username);
            continue; // retry
        }

        if (p->pw_passwd[0] == '!' || p->pw_passwd[0] == '*') {
            printf("user %s is locked\n",username);
            continue; // retry
        }

        if (!p->pw_passwd[0]) {
            break; // no password required
        }

        struct termios oflags, nflags;

        /* disabling echo */
        tcgetattr(fileno(stdin),&oflags);
        nflags = oflags;
        nflags.c_lflag &= ~ECHO;
        nflags.c_lflag |= ECHONL;

        if (tcsetattr(fileno(stdin),TCSANOW,&nflags) != 0) {
            perror("tcsetattr");
            exit(EXIT_FAILURE);
        }

        int n = prompt("password: ",password,sizeof(password));

        /* restore terminal */
        if (tcsetattr(fileno(stdin),TCSANOW,&oflags) != 0) {
            perror("tcsetattr");
            exit(EXIT_FAILURE);
        }

        if (n) {
            char* hash = crypt(password,p->pw_passwd);
            if (hash && !strcmp(hash,p->pw_passwd)) {
                break; // password matches
            }
        }

        puts("login error\n");

    } // for (count)

    alarm(0);

    if (!p) {
        puts("retry limit\n");
        exit(EXIT_FAILURE);
    }

    if ( initgroups(p->pw_name,p->pw_gid) ||
         setgid(p->pw_gid) ||
         setuid(p->pw_uid) )
    {
        perror("setuser");
        exit(EXIT_FAILURE);
    }

    putchar('\n');

    setenv("USER",p->pw_name,1);
    setenv("LOGNAME",p->pw_name,1);
    setenv("HOME",p->pw_dir,1);
    setenv("SHELL",p->pw_shell,1);

    char* arg0 = calloc(strlen(p->pw_shell)+2,sizeof(unsigned char));
    arg0[0] = '-'; strcpy(arg0+1,basename(p->pw_shell));

    execl(p->pw_shell,arg0,NULL);

    perror("shell");
    exit(EXIT_FAILURE);
    return 0;
}
