#include <stdio.h>
#include <stdlib.h>

#include "shell.h"
#include "xtimer.h"

#include "9pfs.h"
#include "vfs.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#ifndef NINEPFS_HOST
  #define NINEPFS_HOST "fe80::dc64:b4ff:fec7:b25d"
#endif

#ifndef NINEPFS_PORT
  #define NINEPFS_PORT 5640
#endif

int
main(void)
{
	int ret;
	sock_tcp_ep_t remote = SOCK_IPV6_EP_ANY;
	char line_buf[SHELL_DEFAULT_BUFSIZE];
	vfs_mount_t mountp;

	remote.port = NINEPFS_PORT;
	if (!ipv6_addr_from_str((ipv6_addr_t *)&remote.addr, NINEPFS_HOST)) {
		fprintf(stderr, "Address '%s' is malformed\n", NINEPFS_HOST);
		return EXIT_FAILURE;
	}

	mountp.mount_point = "/9pfs";
	mountp.fs = &_9p_file_system;
	mountp.private_data = &remote;

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

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

	return EXIT_SUCCESS;
}
