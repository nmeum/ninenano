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

/**
 * Various global variables.
 */
enum {
	MAXSIZ = 128, /**< Maximum size of buffers located on the stack. */
};

/**
 * Endpoint for the protocol socket.
 */
static sock_tcp_ep_t remote = SOCK_IPV6_EP_ANY;

/**
 * Rootfid newly initialized for each test.
 */
static _9pfid *rootfid;

static void
set_up(void)
{
	if (_9pinit(remote))
		TEST_FAIL("_9pinit failed");

	TEST_ASSERT_EQUAL_INT(0, _9pversion());
	TEST_ASSERT_EQUAL_INT(0, _9pattach(&rootfid, "test", NULL));
}

static void
tear_down(void)
{
	_9pclose();
}

static void
test_9pfs__read(void)
{
	size_t n;
	_9pfid *fid;
	char dest[MAXSIZ];

	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&fid, "foo/bar/hello"));
	TEST_ASSERT_EQUAL_INT(0, _9popen(fid, OREAD));

	n = _9pread(fid, dest, MAXSIZ - 1);
	TEST_ASSERT_EQUAL_INT(n, 13);

	dest[n] = '\0';
	TEST_ASSERT_EQUAL_STRING("Hello World!\n", (char*)dest);

	TEST_ASSERT_EQUAL_INT(0, _9pclunk(fid));
}

static void
test_9pfs_create_and_delete(void)
{
	_9pfid *fid;

	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&fid, "foo"));
	TEST_ASSERT_EQUAL_INT(0, _9pcreate(fid, "falafel", ORDWR, OTRUNC));

	TEST_ASSERT_EQUAL_INT(0, _9premove(fid));
	TEST_ASSERT(_9pwalk(&fid, "foo/falafel") != 0); /* TODO better check. */
}

static void
test_9pfs__write(void)
{
	_9pfid *fid;
	char *str = "foobar";
	char dest[7];
	size_t n;

	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&fid, "writeme"));
	TEST_ASSERT_EQUAL_INT(0, _9popen(fid, ORDWR|OTRUNC));

	n = _9pwrite(fid, str, 6);
	TEST_ASSERT_EQUAL_INT(6, n);

	n = _9pread(fid, dest, 6);
	dest[6] = '\0';

	TEST_ASSERT_EQUAL_STRING(str, (char*)dest);
	TEST_ASSERT_EQUAL_INT(0, _9pclunk(fid));
}

Test*
tests_9pfs_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9pfs__read),
		new_TestFixture(test_9pfs_create_and_delete),
		new_TestFixture(test_9pfs__write),
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
