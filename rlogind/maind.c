#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>


int warn(char *msg, ...) {
    va_list va;
    va_start(va,msg);
    vfprintf(stderr,msg,va);
    va_end(va);
    fputc('\n',stderr);
    return errno;
}

void fail(char *msg) {
    perror(msg);
    exit(errno);
}

static void child_end(int signo) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
    signal(signo, &child_end);
}

extern int rlogind_main(struct sockaddr *, socklen_t);

int main(int argc, char *argv[]) {

    int server;
    struct sockaddr_in s_addr;

    bzero(&s_addr, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = getservbyname("login","tcp")->s_port;
    s_addr.sin_addr.s_addr = INADDR_ANY;

    if ( (server = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) fail("socket");
    if ( bind(server, (struct sockaddr*)&s_addr, sizeof(s_addr)) != 0 ) fail("bind");
    if ( listen(server, 20) != 0 ) fail("listen");

    signal(SIGCHLD, &child_end);
    // daemon(0, 0);

    for (;;) {
        int client;
        struct sockaddr_in c_addr;
        unsigned int n_addr = sizeof(c_addr);

        if ( (client = accept(server, (struct sockaddr*)&c_addr, &n_addr)) < 0) {
            perror("accept");
        } else {
            pid_t pid = fork();
            if (pid < 0) { // error
                perror("fork");
            }
            else {
                if (pid > 0) { // parent
                    close(client);
                }
                else { // pid = 0 // child
                    dup2(client, STDIN_FILENO);
                    dup2(client, STDOUT_FILENO);
                    // dup2(client, STDERR_FILENO);
                    // close(STDERR_FILENO);

                    rlogind_main((struct sockaddr *)&c_addr,n_addr);
                }
            }
        }
    }

    close(server);
    return 0;
}
