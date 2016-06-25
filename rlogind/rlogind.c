/*
    rlogind.c - remote login server
    Copyright (C) 2003  Guus Sliepen <guus@sliepen.eu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pty.h>
#include <utmp.h>
#include <grp.h>


extern void warn(char*, ...);
#define syslog(a,...) warn(__VA_ARGS__)


/* Make sure everything gets written */

static ssize_t safewrite(int fd, const void *buf, size_t count) {
    int result;

    while (count) {
        result = write(fd, buf, count);
        if (result <= 0) {
            return -1;
        }
        buf += result;
        count -= result;
    }

    return count;
}

/* Read until a NULL byte is encountered */

static ssize_t readtonull(int fd, char *buf, size_t count) {
    int len = 0, result;

    while (count) {
        result = read(fd, buf, 1);

        if (result <= 0) {
            return result;
        }
        len++;
        count--;

        if (!*buf++) {
            return len;
        }
    }

    errno = ENOBUFS;
    return -1;
}


int rlogind_main(struct sockaddr * peer, socklen_t peerlen) {

    char user[1024];
    char luser[1024];
    char term[1024];

    int portnr;


    int err;


    char host[NI_MAXHOST];
    char addr[NI_MAXHOST];
    char port[NI_MAXSERV];

    char buf[4096];
    int len;

    struct pollfd pfd[3];

    struct winsize winsize;
    uint16_t winbuf[4];
    int i;

    int master, slave;
    char *tty, *ttylast;


    int pid;


    /* Unmap V4MAPPED addresses */

    if (peer->sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&((struct sockaddr_in6 *)peer)->sin6_addr)) {
        ((struct sockaddr_in *)peer)->sin_addr.s_addr = ((struct sockaddr_in6 *)peer)->sin6_addr.s6_addr32[3];
        peer->sa_family = AF_INET;
    }

    /* Lookup hostname */

    if ((err = getnameinfo(peer, peerlen, host, sizeof host, NULL, 0, 0))) {
        syslog(LOG_ERR, "Error resolving address: %s", gai_strerror(err));
        return 1;
    }

    if ((err = getnameinfo(peer, peerlen, addr, sizeof addr, port, sizeof port, NI_NUMERICHOST | NI_NUMERICSERV))) {
        syslog(LOG_ERR, "Error resolving address: %s", gai_strerror(err));
        return 1;
    }

    /* Check if connection comes from a privileged port */

    portnr = atoi(port);

    if (portnr < 512 || portnr >= 1024) {
        syslog(LOG_ERR, "Connection from %s on illegal port %d.", host, portnr);
        return 1;
    }

    /* Wait for NULL byte */

    if (read(0, buf, 1) != 1 || *buf) {
        syslog(LOG_ERR, "Didn't receive NULL byte from %s: %m\n", host);
        return 1;
    }

    /* Read usernames and terminal info */

    if (readtonull(0, user, sizeof user) <= 0 || readtonull(0, luser, sizeof luser) <= 0) {
        syslog(LOG_ERR, "Error while receiving usernames from %s: %m", host);
        return 1;
    }

    if (readtonull(0, term, sizeof term) <= 0) {
        syslog(LOG_ERR, "Error while receiving terminal from %s: %m", host);
        return 1;
    }

    syslog(LOG_NOTICE, "Connection from %s@%s for %s", user, host, luser);

    /* We need to have a pty before we can use PAM */

    if (openpty(&master, &slave, 0, 0, &winsize) != 0) {
        syslog(LOG_ERR, "Could not open pty: %m");
        return 1;
    }

    tty = ttyname(slave);


    /* Write NULL byte to client so we can give a login prompt if necessary */

    if (safewrite(1, "", 1) == -1) {
        syslog(LOG_ERR, "Unable to write NULL byte: %m");
        return 1;
    }


    if ((pid = fork()) < 0) {
        syslog(LOG_ERR, "fork() failed: %m");
        return 1;
    }

    if (send(1, "\x80", 1, MSG_OOB) <= 0) {
        syslog(LOG_ERR, "Unable to write OOB \x80: %m");
        return 1;
    }

    if (pid) {
        /* Parent process, still the rlogin server */

        close(slave);

        /* Process input/output */

        pfd[0].fd = 0;
        pfd[0].events = POLLIN | POLLERR | POLLHUP;
        pfd[1].fd = master;
        pfd[1].events = POLLIN | POLLERR | POLLHUP;

        for (;;) {
            errno = 0;

            if (poll(pfd, 2, -1) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            if (pfd[0].revents) {
                len = read(0, buf, sizeof buf);
                if (len <= 0) {
                    break;
                }

                /* Scan for control messages. Yes this is evil and should be done differently. */

                for (i = 0; i < len - 11;) {
                    if (buf[i++] == (char)0xFF)
                    if (buf[i++] == (char)0xFF)
                    if (buf[i++] == 's')
                    if (buf[i++] == 's') {
                        memcpy(winbuf, buf + i, 8);
                        winsize.ws_row = ntohs(winbuf[0]);
                        winsize.ws_col = ntohs(winbuf[1]);
                        winsize.ws_xpixel = ntohs(winbuf[2]);
                        winsize.ws_ypixel = ntohs(winbuf[3]);
                        if (ioctl(master, TIOCSWINSZ, &winsize) == -1) {
                            break;
                        }
                        memcpy(buf + i - 4, buf + i + 8, len - i - 8);
                        i -= 4;
                        len -= 12;
                    }
                }

                if (safewrite(master, buf, len) == -1) {
                    break;
                }
                pfd[0].revents = 0;
            }

            if (pfd[1].revents) {
                len = read(master, buf, sizeof buf);
                if (len <= 0) {
                    errno = 0;
                    break;
                }
                if (safewrite(1, buf, len) == -1) {
                    break;
                }
                pfd[1].revents = 0;
            }
        }

        /* The end */

        if (errno) {
            syslog(LOG_NOTICE, "Closing connection with %s@%s: %m", user, host);
            err = 1;
        } else {
            syslog(LOG_NOTICE, "Closing connection with %s@%s", user, host);
            err = 0;
        }

        ttylast = tty + 5;

        if (logout(ttylast)) {
            logwtmp(ttylast, "", "");
        }
        close(master);
    }
    else {
        /* Child process, will become the shell */

        struct termios tios;
        char *envp[2];

        /* Prepare tty for login */

        close(master);
        if (login_tty(slave)) {
            syslog(LOG_ERR, "login_tty() failed: %m");
            return 1;
        }

        /* Fix terminal type and speed */

        tcgetattr(0, &tios);

        tcsetattr(0, TCSADRAIN, &tios);

        /* Create environment */

        asprintf(&envp[0], "TERM=%s", term);
        envp[1] = NULL;

        /* Spawn login process */

        execle("/sbin/login", "login", "-p", user, NULL, envp);

        syslog(LOG_ERR, "Failed to spawn login process: %m");
        return 1;
    }

    return err;
}
