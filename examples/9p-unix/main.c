#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "9p.h"

static int sockfd;
static _9pctx ctx;

#define NINEPFS_FN  "hello"
#define NINEPFS_STR "Hello World!\n"

static ssize_t
recvfn(void *buf, size_t count)
{
	return read(sockfd, buf, count);
}

static ssize_t
sendfn(void *buf, size_t count)
{
	return write(sockfd, buf, count);
}

int
writestr(void)
{
	int r;
	_9pfid *fid, *rfid;

	if ((r = _9pversion(&ctx)))
		return r;
	if ((r = _9pattach(&ctx, &rfid, "glenda", NULL)))
		return r;

	if ((r = _9pwalk(&ctx, &fid, "/")))
		return r;
	if ((r = _9pcreate(&ctx, fid, NINEPFS_FN,
			S_IRUSR|S_IWUSR, OWRITE|OTRUNC)))
		return r;

	if ((r = _9pwrite(&ctx, fid, NINEPFS_STR,
			strlen(NINEPFS_STR))) < 0)
		return r;

	_9pclunk(&ctx, fid);
	_9pclunk(&ctx, rfid);

	return 0;
}

int
main(int argc, char **argv)
{
	int r;
	struct addrinfo *addr, *a;
	struct addrinfo hints;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s HOST PORT\n", argv[0]);
		return EXIT_FAILURE;
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if ((r = getaddrinfo(argv[1], argv[2], &hints, &addr))) {
		fprintf(stderr, "getaddrinfo failed: %d\n", r);
		return EXIT_FAILURE;
	}

	for (sockfd = -1, a = addr; a; sockfd = -1, a = a->ai_next) {
		if ((sockfd = socket(a->ai_family, a->ai_socktype,
				a->ai_protocol)) == -1)
			continue;

		if (!connect(sockfd, a->ai_addr, a->ai_addrlen))
			break;
		close(sockfd);
	}

        freeaddrinfo(addr);
	if (sockfd == -1) {
		perror("couldn't connect to server");
		return EXIT_FAILURE;
	}

	_9pinit(&ctx, recvfn, sendfn);
	if ((r = writestr())) {
		errno = r * -1;
		perror("writestr failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
