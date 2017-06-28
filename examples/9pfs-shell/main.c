#include <stdio.h>
#include <stdlib.h>

#include "shell.h"
#include "xtimer.h"

#include "9p.h"
#include "9pfs.h"
#include "vfs.h"

#include "net/af.h"
#include "net/gnrc/ipv6.h"
#include "net/gnrc/tcp.h"

#ifndef NINEPFS_HOST
  #define NINEPFS_HOST "fe80::e806:5fff:feca:411c"
#endif

#ifndef NINEPFS_PORT
  #define NINEPFS_PORT 5640
#endif

/**
 * GNRC transmission control block.
 */
static gnrc_tcp_tcb_t tcb;

/**
 * 9P connection context.
 */
static _9pctx ctx;

/* import "ifconfig" shell command, used for printing addresses */
extern int _netif_config(int argc, char **argv);

static ssize_t
recvfn(void *buf, size_t count)
{
	return gnrc_tcp_recv(&tcb, buf,
		count, GNRC_TCP_CONNECTION_TIMEOUT_DURATION);
}

static ssize_t
sendfn(void *buf, size_t count)
{
	return gnrc_tcp_send(&tcb, buf,
		count, GNRC_TCP_CONNECTION_TIMEOUT_DURATION);
}

int
main(void)
{
	int ret;
	ipv6_addr_t remote;
	char line_buf[SHELL_DEFAULT_BUFSIZE];
	vfs_mount_t mountp;

	if (!ipv6_addr_from_str(&remote, NINEPFS_HOST)) {
		fprintf(stderr, "Address '%s' is malformed\n", NINEPFS_HOST);
		return EXIT_FAILURE;
	}

	mountp.mount_point = "/9pfs";
	mountp.fs = &_9p_file_system;
	mountp.private_data = &ctx;

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	puts("Configured network interfaces:");
	_netif_config(0, NULL);

	gnrc_tcp_tcb_init(&tcb);
	if ((ret = gnrc_tcp_open_active(&tcb, AF_INET6,
			(uint8_t *)&remote, NINEPFS_PORT, 0))) {
		fprintf(stderr, "Couldn't open TCP connection\n");
		return EXIT_FAILURE;
	}

	_9pinit(&ctx, recvfn, sendfn);

	if ((ret = vfs_mount(&mountp))) {
		fprintf(stderr, "vfs_mount failed, err code: %d\n", ret);
		return EXIT_FAILURE;
	}

	puts("All up, running the shell now");
	shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

	if ((ret = vfs_umount(&mountp))) {
		fprintf(stderr, "vfs_unmount failed, err code: %d\n", ret);
		return EXIT_FAILURE;
	}

	gnrc_tcp_close(&tcb);
	return EXIT_SUCCESS;
}
