#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "9pfs.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#include "embUnit.h"

/**
 * Retrieve value of environment variable using getenv(3) and return
 * `EXIT_FAILURE` if the result was a NULL pointer.
 *
 * @param VAR Name of the variable to store result in.
 * @param ENV Name of the environment variable.
 */
#define GETENV(VAR, ENV) \
	do { if (!(VAR = getenv(ENV))) { \
		printf("%s is not set or empty\n", ENV); \
		return EXIT_FAILURE; } \
	} while (0)

enum {
	MAXSIZ = 128,
};

static sock_tcp_ep_t remote = SOCK_IPV6_EP_ANY;

static void
set_up(void)
{
	if (_9pinit(remote))
		TEST_FAIL("_9pinit failed");
}

static void
tear_down(void)
{
	_9pclose();
}

static void
test_9pfs__read_file(void)
{
	size_t n;
	_9pfid *rootfid, *fid;
	char dest[MAXSIZ];

	TEST_ASSERT_EQUAL_INT(0, _9pversion());
	TEST_ASSERT_EQUAL_INT(0, _9pattach(&rootfid, "foo", NULL));
	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&fid, "foo/bar/hello"));
	TEST_ASSERT_EQUAL_INT(0, _9popen(fid, OREAD));

	n = _9pread(fid, dest, MAXSIZ - 1);
	TEST_ASSERT_EQUAL_INT(n, 13);

	dest[n] = '\0';
	TEST_ASSERT_EQUAL_STRING("Hello World!\n", (char*)dest);

	TEST_ASSERT_EQUAL_INT(0, _9pclunk(fid));
}

Test*
tests_9pfs_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9pfs__read_file),
	};

	EMB_UNIT_TESTCALLER(_9pfs_tests, set_up, tear_down, fixtures);
	return (Test*)&_9pfs_tests;
}

int
main(void)
{
	char *addr, *port;

	GETENV(addr, "NINERIOT_ADDR");
	GETENV(port, "NINERIOT_PPORT");

	remote.port = atoi(port);
	ipv6_addr_from_str((ipv6_addr_t *)&remote.addr, addr);

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	TESTS_START();
	TESTS_RUN(tests_9pfs_tests());
	TESTS_END();

	return EXIT_SUCCESS;
}
