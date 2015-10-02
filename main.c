#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int ret;
	pid_t p;

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
		/* Child */
		execv(argv[1], &argv[1]);
		return errno;
	}
	waitpid(p, &ret, 0);
	return ret;
}
