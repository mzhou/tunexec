#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h> /* bug in if.h */
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t p;
int tun_parent;
int tun_child;
fd_set readfds;
int nfds;
int waited;
int ret;

void sig_handler(int signum);
void sigchld_handler(int signum);
void sigchld_handler(int signum);
int logged_system(const char* command);
void process_tuns();

int main(int argc, char *argv[])
{
	int err;
	int readyfd[2];
	char readybuf[1];
	sigset_t blockset;
	struct ifreq ifr;
	const char* tun_parent_name;
	const char* tun_child_name;
	char cmdbuf[128];

	if ((err = pipe2(readyfd, O_CLOEXEC)) != 0) {
		fprintf(stderr, "pipe2: %d %d %s\n", err, errno, strerror(errno));
		return err;
	}

	readybuf[0] = 0;

	p = 0;
	waited = 0;
	signal(SIGTERM, sig_handler);
	signal(SIGCONT, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGALRM, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGCHLD, sigchld_handler);
	/* block SIGCHLD until pselect */
	sigemptyset(&blockset);
	sigaddset(&blockset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &blockset, NULL);

	p = syscall(
		SYS_clone,
		SIGCHLD
			| CLONE_NEWIPC
			| CLONE_NEWNET
			| CLONE_NEWNS
			| CLONE_NEWPID
			| CLONE_NEWUTS,
		NULL,
		NULL,
		NULL);
	if (p == 0) {
		/* child */
		(void) read(readyfd[0], readybuf, 1); /* wait for tun */
		err = execv(argv[4], &argv[4]);
		fprintf(stderr, "execv %d %d %s\n", err, errno, strerror(errno));
		return errno;
	}

	FD_ZERO(&readfds);
	if ((tun_parent = open("/dev/net/tun", O_RDWR)) < 0) {
		fprintf(stderr, "open tun_parent\n");
		goto tun_finished;
	}
	FD_SET(tun_parent, &readfds);
	nfds = tun_parent + 1;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	tun_parent_name = argv[1];
	strncpy(ifr.ifr_name, argv[1], IFNAMSIZ);
	if ((err = ioctl(tun_parent, TUNSETIFF, &ifr)) < 0) {
		fprintf(stderr, "ioctl tun_parent\n");
		goto tun_parent_unwind;
	}

	if ((tun_child = open("/dev/net/tun", O_RDWR)) < 0) {
		fprintf(stderr, "open tun_child\n");
		goto tun_parent_unwind;
	}
	FD_SET(tun_child, &readfds);
	if (tun_child + 1 > nfds) {
		nfds = tun_child + 1;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	tun_child_name = argv[2];
	strncpy(ifr.ifr_name, tun_child_name, IFNAMSIZ);
	if ((err = ioctl(tun_child, TUNSETIFF, &ifr)) < 0) {
		fprintf(stderr, "ioctl tun_child\n");
		goto tun_child_unwind;
	}

	sprintf(cmdbuf, "/sbin/ip link set dev %s netns %d", tun_child_name, p);
	logged_system(cmdbuf);
	sprintf(cmdbuf, "/sbin/ip ad ad %s dev %s", argv[3], tun_parent_name);
	logged_system(cmdbuf);
	sprintf(cmdbuf, "/sbin/ip link set up dev %s", tun_parent_name);
	logged_system(cmdbuf);

	goto tun_finished;

	tun_child_unwind:
	close(tun_child);
	tun_child = -1;

	tun_parent_unwind:
	close(tun_parent);
	tun_parent = -1;

	tun_finished:
	(void) write(readyfd[1], readybuf, 1);
	close(readyfd[0]);
	close(readyfd[1]);

	if (tun_child >= 0 && tun_parent >= 0) {
		while (!waited) {
			process_tuns();
		}
	} else {
		while (waitpid(p, &ret, 0) != p) {
		}
	}

	return ret;
}

void sig_handler(int signum)
{
	if (p > 0) {
		kill(p, signum);
	}
}

void sigchld_handler(int signum)
{
	int saved_errno;

	saved_errno = errno;
	if (waitpid(p, &ret, WNOHANG) == p) {
		waited = 1;
	}
	errno = saved_errno;
}

int logged_system(const char* command)
{
	int err;

	if ((err = system(command)) != 0) {
		fprintf(stderr, "system(%s) %d %d %s\n", command, err, errno, strerror(errno));
	}
	return err;
}

void process_tuns()
{
	fd_set out_readfds;
	sigset_t sigmask;
	int num_ready;
	size_t bytes;
	char buf[65536];

	out_readfds = readfds;

	sigemptyset(&sigmask);

	num_ready = pselect(nfds, &out_readfds, NULL, NULL, NULL, &sigmask);
	if (num_ready <= 0) {
		return;
	}

	if (FD_ISSET(tun_parent, &out_readfds)) {
		bytes = read(tun_parent, buf, sizeof(buf));
		write(tun_child, buf, bytes);
	}
	if (FD_ISSET(tun_child, &out_readfds)) {
		bytes = read(tun_child, buf, sizeof(buf));
		write(tun_parent, buf, bytes);
	}
}
