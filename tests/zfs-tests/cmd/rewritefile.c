/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/fs/zfs.h>

static int
usage(void)
{
	printf("usage: rewritefile [opts] <file>\n");
	return (1);
}

int do_rewrite(int fd);

int quiet = 0;

int
main(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "q")) != -1) {
		switch (c) {
			case 'q':
				quiet = 1;
				break;
		}
	}

	if ((argc-optind) != 1)
		return (usage());

	int fd = open(argv[optind], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open: %s: %s\n",
		    argv[optind], strerror(errno));
		return (1);
	}

	int err = do_rewrite(fd);

	close(fd);

	return (err == 0 ? 0 : 1);
}

int
do_rewrite(int fd)
{
	int err = ioctl(fd, ZFS_IOC_REWRITE, 0);
	if (err < 0) {
		fprintf(stderr, "rewrite: %s\n", strerror(errno));
		return (err);
	}
	return (0);
}
