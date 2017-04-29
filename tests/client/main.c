#include "9pfs.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#include "embUnit.h"

#define REMOTE_ADDR "fe80::647e:10ff:fed7:926"

static sock_tcp_t ctlsock;

static void
setcmd(char *cmd)
{
	if (sock_tcp_write(&ctlsock, cmd, strlen(cmd)) < 0)
		TEST_FAIL("Couldn't write to control server");
}

static void
set_up(void)
{
	sock_tcp_ep_t pr = SOCK_IPV6_EP_ANY,
		      cr = SOCK_IPV6_EP_ANY;

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	pr.port = PPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&pr.addr, REMOTE_ADDR);

	cr.port = CPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&cr.addr, REMOTE_ADDR);

	if (sock_tcp_connect(&ctlsock, &cr, 0, SOCK_FLAGS_REUSE_EP) < 0)
		TEST_FAIL("Couldn't connect to control server");
	if (_9pinit(pr))
		TEST_FAIL("_9pinit failed");
}

static void
tear_down(void)
{
	_9pclose();
	sock_tcp_disconnect(&ctlsock);
}

static void
test_9pfs__rversion_success(void)
{
	setcmd("rversion_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pversion());
}

static void
test_9pfs__rversion_unknown(void)
{
	setcmd("rversion_unknown\n");
	TEST_ASSERT_EQUAL_INT(-ENOPROTOOPT, _9pversion());
}

static void
test_9pfs__rversion_msize_too_big(void)
{
	setcmd("rversion_msize_too_big\n");
	TEST_ASSERT_EQUAL_INT(-EMSGSIZE, _9pversion());
}

static void
test_9pfs__rversion_invalid(void)
{
	setcmd("rversion_invalid\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion());
}

Test
*tests_9pfs_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9pfs__rversion_success),
		new_TestFixture(test_9pfs__rversion_unknown),
		new_TestFixture(test_9pfs__rversion_msize_too_big),
		new_TestFixture(test_9pfs__rversion_invalid),
	};

	EMB_UNIT_TESTCALLER(_9pfs_tests, set_up, tear_down, fixtures);
	return (Test*)&_9pfs_tests;
}

int
main(void)
{
	TESTS_START();
	TESTS_RUN(tests_9pfs_tests());
	TESTS_END();

	return 0;
}
