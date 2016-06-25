/*
 * logind.c - login daemon for virtual console terminals
 *
 * Copyright 2007 Andre Oliveira
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Synopsis: logind tty...
 *
 * logind open()s the virtual console terminals specified as arguments and
 * poll()s them for user input.  Once input is available on one of the
 * terminals, it fork()s and exec()s the login(1) program with that terminal
 * set up as the controlling terminal.  When a login session terminates,
 * the sigchld() handler restarts the corresponding terminal.
 */

#include <fcntl.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Pathname of the login(1) program. */
#define LOGIN "/sbin/login"

/* Pathnames of the managed virtual console terminals (argv + 1). */
static char **tty;

/* Number of managed terminals (argc - 1). */
static unsigned npfd;

/*
 * Poll data for the terminals.
 * When polling for input on a terminal, the fd field stores its
 * file descriptor, as customary.  After fork()ing, though, the parent process
 * closes the terminal and stores the child's _negative_ pid in the fd field,
 * so that poll() ignores that entry and sigchld() knows every child's pid.
 */
static struct pollfd *pfd;

/*
 * Jump point to the poll() call in main().
 * Place to go after handling SIGCHLD, to avoid race with poll().
 */
static sigjmp_buf polljmp;

/*
 * Open virtual console terminal i.
 *
 * In case of failure, most likely because the device node does not exist,
 * poll() will simply ignore this entry and users get a blank screen and
 * locked keyboard on this console.  That's enough error reporting. ;-)
 * No need to add code to check all syscalls' return values.
 *
 * Linux does not implement revoke(); the *BSD do.
 */
static void opentty(unsigned i)
{
	const char *path = tty[i];
	int fd;

	chown(path, 0, 0);
	chmod(path, S_IRUSR | S_IWUSR);
#ifdef HAVE_REVOKE
	revoke(path);
#endif
	fd = open(path, O_RDWR | O_NOCTTY);
	write(fd, "\nPress ENTER to start this console.", 35);

	pfd[i].fd = fd;
}

/*
 * Execute login(1) on terminal i.
 *
 * Leave it to the login(1) program to complain if, in the unlikely event
 * any syscall fails, the controlling terminal is not set up properly.
 */
static void execlogin(unsigned i)
{
	int fd;
	unsigned j;

	for (fd = 0; fd <= 2; fd++)
		dup2(pfd[i].fd, fd);

	for (j = 0; j < npfd; j++)
		if (pfd[j].fd > 2)
			close(pfd[j].fd);

	setsid();
	ioctl(0, TIOCSCTTY, 0);
	tcflush(0, TCIFLUSH);

	execl(LOGIN, LOGIN, NULL);
}

/*
 * Fork process for login(1) on terminal i.
 *
 * If fork() fails, presumably because of temporary system resource
 * exhaustion, we try again on the next run of handling poll() events.
 */
static void forktty(unsigned i)
{
	pid_t pid;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	pid = fork();
	if (pid > 0) {
		close(pfd[i].fd);
		pfd[i].fd = -pid;
	}
	sigprocmask(SIG_UNBLOCK, &mask, NULL);

	if (pid == 0) {
		execlogin(i);
		_exit(EXIT_FAILURE);
	}
}

/*
 * SIGCHLD handler.
 * Reset terminals of defunct login sessions.
 * Return to safe place in main(), preventing race with poll().
 */
static void sigchld(int signum)
{
	pid_t pid;
	unsigned i;

	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
		for (i = 0; i < npfd; i++) {
			if (pfd[i].fd == -pid) {
				opentty(i);
				break;
			}
		}
	}
	siglongjmp(polljmp, 1);
}

/*
 * Synopsis: logind tty...
 *
 * Since each terminal's struct pollfd only takes up 8 bytes, just alloca()te
 * the pfd array on the stack, instead of using the bloated malloc().
 */
int main(int argc, char **argv)
{
	unsigned i;
	struct sigaction sa;

	if (fork() != 0) exit(0);
	umask(0);
	setsid();
	vhangup();
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	tty = argv + 1;
	npfd = argc - 1;
	pfd = alloca(npfd * sizeof(*pfd));

	for (i = 0; i < npfd; i++) {
		opentty(i);
		pfd[i].events = POLLIN;
	}

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	sa.sa_handler = sigchld;
	sigaction(SIGCHLD, &sa, NULL);

	sigsetjmp(polljmp, 1);

	while (poll(pfd, npfd, -1) > 0)
		for (i = 0; i < npfd; i++)
			if (pfd[i].revents)
				forktty(i);

	return EXIT_FAILURE;
}
