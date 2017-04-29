#include <stdio.h>

#include "9pfs.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#define REMOTE_ADDR "fe80::b1:dbff:fe67:fbac"

static sock_tcp_t ctlsock;

int
main(void)
{
	int r;
	sock_tcp_ep_t pr = SOCK_IPV6_EP_ANY,
		      cr = SOCK_IPV6_EP_ANY;

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	pr.port = PPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&pr.addr, REMOTE_ADDR);

	cr.port = CPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&cr.addr, REMOTE_ADDR);

	puts("Connecting to control server...");
	if (sock_tcp_connect(&ctlsock, &cr, 0, 0) < 0) {
		puts("Couldn't connect to control server");
		return 1;
	}

	puts("Setting command on control server...");
	if (sock_tcp_write(&ctlsock, "test_rversion_success", 21) < 0) {
		puts("Could't write to control server");
		return 1;
	}

	if ((r = _9pinit(pr)))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
