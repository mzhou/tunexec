#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t p;

void sig_handler(int signum);

int main(int argc, char *argv[])
{
	int ret;

	p = syscall(
		SYS_clone,
		SIGCHLD
			| CLONE_NEWIPC
#if 0
			| CLONE_NEWNET
#endif
			| CLONE_NEWNS
			| CLONE_NEWPID
			| CLONE_NEWUTS,
		NULL,
		NULL,
		NULL);
	if (p == 0) {
		/* Child */
		execv(argv[1], &argv[1]);
		return errno;
	}
	signal(SIGTERM, sig_handler);
	signal(SIGCONT, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGALRM, sig_handler);
	signal(SIGINT, sig_handler);
	waitpid(p, &ret, 0);
	return ret;
}

void sig_handler(int signum)
{
	kill(p, signum);
}
