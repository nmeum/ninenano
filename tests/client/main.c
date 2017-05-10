#include "9pfs.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#include "embUnit.h"

/* TCP control socket */
static sock_tcp_t ctlsock;

/* Remote address for control socket */
static sock_tcp_ep_t cr = SOCK_IPV6_EP_ANY;

/* Remote address for 9P protocol socket */
static sock_tcp_ep_t pr = SOCK_IPV6_EP_ANY;

static void
test_9putil__fidtbl_add(void)
{
	_9pfid *f;

	f = _fidtbl(23, ADD);

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_INT(0, f->fid);
}

static void
test_9putil__fidtbl_add_invalid(void)
{
	TEST_ASSERT_NULL(_fidtbl(0, ADD));
}

static void
test_9putil__fidtbl_add_full(void)
{
	_9pfid *f;
	size_t i;

	for (i = 1; i <= _9P_MAXFIDS; i++) {
		f = _fidtbl(i, ADD);
		f->fid = i;
	}

	TEST_ASSERT_NULL(_fidtbl(++i, ADD));
}

static void
test_9putil__fidtbl_get(void)
{
	_9pfid *f1, *f2;

	f1 = _fidtbl(42, ADD);
	f1->fid = 42;
	strcpy(f1->path, "foobar");

	f2 = _fidtbl(42, GET);

	TEST_ASSERT_NOT_NULL(f2);
	TEST_ASSERT_EQUAL_INT(42, f2->fid);
	TEST_ASSERT_EQUAL_STRING("foobar", (char*)f2->path);
}

static void
test_9putil__fidtbl_delete(void)
{
	_9pfid *f1, *f2;

	f1 = _fidtbl(1337, ADD);
	f1->fid = 1337;

	f2 = _fidtbl(1337, DEL);

	TEST_ASSERT_NOT_NULL(f2);
	TEST_ASSERT_EQUAL_INT(0, f2->fid);
	TEST_ASSERT_NULL(_fidtbl(1337, GET));
}

static void
test_9putil__fidtbl_delete_rootfid(void)
{
	_fidtbl(_9P_ROOTFID, ADD);
	TEST_ASSERT_NULL(_fidtbl(_9P_ROOTFID, DEL));
}

Test*
tests_9putil_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9putil__fidtbl_add),
		new_TestFixture(test_9putil__fidtbl_add_invalid),
		new_TestFixture(test_9putil__fidtbl_add_full),
		new_TestFixture(test_9putil__fidtbl_get),
		new_TestFixture(test_9putil__fidtbl_delete),
		new_TestFixture(test_9putil__fidtbl_delete_rootfid),
	};

	/* Use _9pclose as tear down function to reset the fid table. */
	EMB_UNIT_TESTCALLER(_9putil_tests, NULL, _9pclose, fixtures);
	return (Test*)&_9putil_tests;
}

/**
 * You might be wondering why there are no comments below this points.
 * This is the case because the purpose of the various test cases is
 * explained in the file `tests/server/tests.go` instead.
 *
 * Please refer to that file if you seek more information about the
 * various test cases. The test cases just act as stupid TCP clients,
 * the interessting stuff happens on the server side.
 */

static void
setcmd(char *cmd)
{
	if (sock_tcp_write(&ctlsock, cmd, strlen(cmd)) < 0)
		TEST_FAIL("Couldn't write to control server");
}

static void
set_up(void)
{
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

static void
test_9pfs__rversion_invalid_len(void)
{
	setcmd("rversion_invalid_len\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion());
}

static void
test_9pfs__rversion_version_too_long(void)
{
	setcmd("rversion_version_too_long\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion());
}

static void
test_9pfs__rattach_success(void)
{
	_9pfid *fid;

	setcmd("rattach_success\n");
	fid = _9pattach("foo", NULL);

	TEST_ASSERT_NOT_NULL(fid);
	TEST_ASSERT_EQUAL_STRING("", (char*)fid->path);
	TEST_ASSERT(fid->fid > 0);
}

static void
test_9pfs__rattach_invalid_len(void)
{
	setcmd("rattach_invalid_len\n");
	TEST_ASSERT_NULL(_9pattach("foobar", NULL));
}

static void
test_9pfs__rstat_success(void)
{
	_9pfid f;
	struct stat st;

	setcmd("rstat_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pstat(&f, &st));

	TEST_ASSERT_EQUAL_INT(23, f.qid.type);
	TEST_ASSERT_EQUAL_INT(2342, f.qid.vers);
	TEST_ASSERT_EQUAL_INT(1337, f.qid.path);

	TEST_ASSERT_EQUAL_INT(S_IFDIR, st.st_mode);
	TEST_ASSERT_EQUAL_INT(1494443596, st.st_atime);
	TEST_ASSERT_EQUAL_INT(1494443609, st.st_mtime);
	TEST_ASSERT_EQUAL_INT(st.st_mtime, st.st_ctime);
	TEST_ASSERT_EQUAL_INT(2342, st.st_size);

	TEST_ASSERT_EQUAL_INT(0, st.st_dev);
	TEST_ASSERT_EQUAL_INT(0, st.st_ino);
	TEST_ASSERT_EQUAL_INT(0, st.st_rdev);
	TEST_ASSERT_EQUAL_INT(1, st.st_nlink);
	TEST_ASSERT_EQUAL_INT(0, st.st_uid);
	TEST_ASSERT_EQUAL_INT(0, st.st_gid);
	TEST_ASSERT_EQUAL_INT(_9P_MSIZE - _9P_IOHDRSIZ, st.st_blksize);
	TEST_ASSERT_EQUAL_INT(2342 / (_9P_MSIZE - _9P_IOHDRSIZ) + 1, st.st_blocks);

	TEST_ASSERT_EQUAL_STRING("testfile", (char*)f.path);
}

Test*
tests_9pfs_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9pfs__rversion_success),
		new_TestFixture(test_9pfs__rversion_unknown),
		new_TestFixture(test_9pfs__rversion_msize_too_big),
		new_TestFixture(test_9pfs__rversion_invalid),
		new_TestFixture(test_9pfs__rversion_invalid_len),
		new_TestFixture(test_9pfs__rversion_version_too_long),

		new_TestFixture(test_9pfs__rattach_success),
		new_TestFixture(test_9pfs__rattach_invalid_len),

		new_TestFixture(test_9pfs__rstat_success),
	};

	EMB_UNIT_TESTCALLER(_9pfs_tests, set_up, tear_down, fixtures);
	return (Test*)&_9pfs_tests;
}

int
main(void)
{
	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	pr.port = PPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&pr.addr, REMOTE_ADDR);

	cr.port = CPORT;
	ipv6_addr_from_str((ipv6_addr_t *)&cr.addr, REMOTE_ADDR);

	TESTS_START();
	TESTS_RUN(tests_9putil_tests());
	TESTS_RUN(tests_9pfs_tests());
	TESTS_END();

	return 0;
}
