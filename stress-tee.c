/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_TEE)

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define TEE_IO_SIZE	(65536)

/*
 *  stress_tee_spawn()
 *	spawn off tee I/O processes
 */
static pid_t stress_tee_spawn(
	const char *name,
	void (*func)(int fds[2]),
	int fds[2])
{
        pid_t pid;

	if (pipe(fds) < 0) {
		pr_err(stderr, "%s: pipe failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}

again:
        pid = fork();
        if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;

		(void)close(fds[0]);
		(void)close(fds[1]);
		pr_err(stderr, "%s: fork failed: %d (%s)\n",
			name, errno, strerror(errno));
                return -1;
        }
        if (pid == 0) {
                func(fds);
                exit(EXIT_SUCCESS);
        }
        return pid;
}

/*
 *  stress_tee_pipe_write()
 *	write data down a pipe
 */
static void stress_tee_pipe_write(int fds[2])
{
	char buffer[TEE_IO_SIZE];

	(void)close(fds[0]);

	memset(buffer, 0, sizeof(buffer));
	for (;;) {
		ssize_t ret;

		ret = write(fds[1], buffer, sizeof(buffer));
		if (ret < 0) {
			if (errno != EAGAIN)
				break;
		}
	}
	(void)close(fds[1]);
}

/*
 *  stress_tee_pipe_read()
 *	read data from a pipe
 */
static void stress_tee_pipe_read(int fds[2])
{
	char buffer[TEE_IO_SIZE];

	(void)close(fds[1]);

	for (;;) {
		ssize_t ret;

		ret = read(fds[0], buffer, sizeof(buffer));
		if (ret < 0)
			if (errno != EAGAIN)
				break;
	}
	(void)close(fds[1]);
}

/*
 *  stress_tee()
 *	stress the Linux tee syscall
 */
int stress_tee(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	ssize_t len, slen;
	int fd, pipe_in[2], pipe_out[2];
	pid_t pids[2];
	int ret = EXIT_FAILURE, status;

	(void)instance;

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0) {
		pr_err(stderr, "%s: open /dev/null failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pids[0] = stress_tee_spawn(name, stress_tee_pipe_write, pipe_in);
	if (pids[0] < 0) {
		close(fd);
		return EXIT_FAILURE;
	}
	(void)close(pipe_in[1]);

	pids[1] = stress_tee_spawn(name, stress_tee_pipe_read, pipe_out);
	if (pids[0] < 0)
		goto tidy_child1;
	(void)close(pipe_out[0]);

	do {
		len = tee(pipe_in[0], pipe_out[1],
			INT_MAX, 0 & SPLICE_F_NONBLOCK);

		if (len < 0) {
			if (errno == EAGAIN)
				continue;
			if (errno == EINTR)
				break;
			pr_err(stderr, "%s: tee failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			goto tidy_child2;
		} else {
			if (len == 0)
				break;
		}

		while (len > 0) {
			slen = splice(pipe_in[0], NULL, fd, NULL,
				len, SPLICE_F_MOVE);
			if (slen < 0) {
				pr_err(stderr, "%s: splice failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				goto tidy_child2;
			}
			len -= slen;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	ret = EXIT_SUCCESS;

tidy_child2:
	(void)close(pipe_out[1]);
	(void)kill(pids[1], SIGKILL);
	(void)waitpid(pids[1], &status, 0);

tidy_child1:
	(void)close(pipe_in[0]);
	(void)kill(pids[0], SIGKILL);
	(void)waitpid(pids[0], &status, 0);

	(void)close(fd);

	return ret;
}
#endif
