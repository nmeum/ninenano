#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "9p.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#include "embUnit.h"
#include "../../util.h"

/**
 * TCP control socket.
 */
static sock_tcp_t csock;

/**
 * Global 9P connection context.
 */
static _9pctx ctx;

static void
setcmd(char *cmd)
{
	printf("RUNNING: %s", cmd);
	if (sock_tcp_write(&csock, cmd, strlen(cmd)) < 0)
		TEST_FAIL("Couldn't write to control server");
}

static void
tear_down(void)
{
	memset(ctx.fids, 0, _9P_MAXFIDS * sizeof(_9pfid));
}

static void
set_up(void)
{
	xtimer_usleep(1000);
}

/**
 * @defgroup _9putil_tests Tests for utility functios from `9p/util.c`.
 *
 * @{
 */

static void
test_9putil_pstring_and_hstring(void)
{
	_9ppkt pkt;
	uint8_t buf[10];
	char dest[10];

	pkt.buf = buf;
	pkt.len = 10;

	TEST_ASSERT_EQUAL_INT(0, pstring("foobar", &pkt));

	pkt.buf = buf;
	pkt.len = 10;

	TEST_ASSERT_EQUAL_INT(0, hstring(dest, 10, &pkt));
	TEST_ASSERT_EQUAL_STRING("foobar", (char*)dest);
}

static void
test_9putil_pstring_empty_string(void)
{
	_9ppkt pkt;
	uint8_t buf[2];
	char dest[2];

	pkt.buf = buf;
	pkt.len = 4;

	TEST_ASSERT_EQUAL_INT(0, pstring(NULL, &pkt));

	pkt.buf = buf;
	pkt.len = 4;

	TEST_ASSERT_EQUAL_INT(0, hstring(dest, 2, &pkt));
	TEST_ASSERT_EQUAL_STRING("", (char*)dest);
}

static void
test_9putil_pstring_buffer_to_small1(void)
{
	_9ppkt pkt;
	uint8_t buf[1];

	pkt.buf = buf;
	pkt.len = 1;

	TEST_ASSERT_EQUAL_INT(-1, pstring(NULL, &pkt));
}

static void
test_9putil_pstring_buffer_to_small2(void)
{
	_9ppkt pkt;
	uint8_t buf[5];

	pkt.buf = buf;
	pkt.len = 5;

	TEST_ASSERT_EQUAL_INT(-1, pstring("lolz", &pkt));
}

static void
test_9putil_hstring_invalid1(void)
{
	_9ppkt pkt;
	uint8_t buf[10];
	char dest[10];

	pkt.buf = buf;
	pkt.len = 10;

	TEST_ASSERT_EQUAL_INT(0, pstring("kek", &pkt));

	pkt.buf = buf;
	pkt.len = BIT16SZ - 1;

	TEST_ASSERT_EQUAL_INT(-1, hstring(dest, 10, &pkt));
}

static void
test_9putil_hstring_invalid2(void)
{
	_9ppkt pkt;
	uint8_t buf[5];
	char dest[5];

	pkt.len = 5;
	pkt.buf = buf;

	htop16(pkt.len, &pkt);

	pkt.len = 5;
	pkt.buf = buf;

	TEST_ASSERT_EQUAL_INT(-1, hstring(dest, 5, &pkt));
}

static void
test_9putil_hstring_invalid3(void)
{
	_9ppkt pkt;
	uint8_t buf[5];
	char dest[5];

	pkt.buf = buf;
	pkt.len = 5;

	TEST_ASSERT_EQUAL_INT(0, pstring("foo", &pkt));

	pkt.buf = buf;
	pkt.len = 5;

	htop16(42, &pkt);

	TEST_ASSERT_EQUAL_INT(-1, hstring(dest, 5, &pkt));
}

static void
test_9putil_fidtbl_add(void)
{
	_9pfid *f;

	f = fidtbl(ctx.fids, 23, ADD);
	f->fid = 23;

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_INT(23, f->fid);
	TEST_ASSERT_EQUAL_INT(1, cntfids(ctx.fids));
}

static void
test_9putil_fidtbl_add_invalid(void)
{
	TEST_ASSERT_NULL(fidtbl(ctx.fids, 0, ADD));
	TEST_ASSERT_EQUAL_INT(0, cntfids(ctx.fids));
}

static void
test_9putil_fidtbl_add_full(void)
{
	_9pfid *f;
	size_t i;

	for (i = 1; i <= _9P_MAXFIDS; i++) {
		f = fidtbl(ctx.fids, i, ADD);
		f->fid = i;
	}

	TEST_ASSERT_NULL(fidtbl(ctx.fids, ++i, ADD));
	TEST_ASSERT_EQUAL_INT(_9P_MAXFIDS, cntfids(ctx.fids));
}

static void
test_9putil_fidtbl_get(void)
{
	_9pfid *f1, *f2;

	f1 = fidtbl(ctx.fids, 42, ADD);
	f1->fid = 42;

	f2 = fidtbl(ctx.fids, 42, GET);

	TEST_ASSERT_NOT_NULL(f2);
	TEST_ASSERT_EQUAL_INT(42, f2->fid);
}

static void
test_9putil_fidtbl_delete(void)
{
	_9pfid *f1, *f2;

	f1 = fidtbl(ctx.fids, 1337, ADD);
	f1->fid = 1337;

	f2 = fidtbl(ctx.fids, 1337, DEL);

	TEST_ASSERT_NOT_NULL(f2);
	TEST_ASSERT_EQUAL_INT(0, f2->fid);
	TEST_ASSERT_NULL(fidtbl(ctx.fids, 1337, GET));
}

static void
test_9putil_fidtbl_delete_rootfid(void)
{
	fidtbl(ctx.fids, _9P_ROOTFID, ADD);
	TEST_ASSERT_NULL(fidtbl(ctx.fids, _9P_ROOTFID, DEL));
}

static void
test_9putil__newfid(void)
{
	_9pfid *f1, *f2;

	f1 = newfid(ctx.fids);
	TEST_ASSERT_NOT_NULL(f1);

	TEST_ASSERT_EQUAL_INT(1, cntfids(ctx.fids));

	f2 = fidtbl(ctx.fids, f1->fid, GET);
	TEST_ASSERT_NOT_NULL(f2);

	TEST_ASSERT_EQUAL_INT(f1->fid, f2->fid);
}

Test*
tests_9putil_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9putil_pstring_and_hstring),
		new_TestFixture(test_9putil_pstring_empty_string),
		new_TestFixture(test_9putil_pstring_buffer_to_small1),
		new_TestFixture(test_9putil_pstring_buffer_to_small2),
		new_TestFixture(test_9putil_hstring_invalid1),
		new_TestFixture(test_9putil_hstring_invalid2),
		new_TestFixture(test_9putil_hstring_invalid3),

		new_TestFixture(test_9putil_fidtbl_add),
		new_TestFixture(test_9putil_fidtbl_add_invalid),
		new_TestFixture(test_9putil_fidtbl_add_full),
		new_TestFixture(test_9putil_fidtbl_get),
		new_TestFixture(test_9putil_fidtbl_delete),
		new_TestFixture(test_9putil_fidtbl_delete_rootfid),

		new_TestFixture(test_9putil__newfid),
	};

	/* Use _9pclose as tear down function to reset the fid table. */
	EMB_UNIT_TESTCALLER(_9putil_tests, NULL, tear_down, fixtures);
	return (Test*)&_9putil_tests;
}

/**@}*/

/**
 * @defgroup _9p_tests Tests for protocol functions from `9p/9p.c`.
 *
 * You might be wondering why there are no comments below this points.
 * This is the case because the purpose of the various test cases is
 * explained in the file `tests/server/tests.go` instead.
 *
 * Please refer to that file if you seek more information about the
 * various test cases. The test cases just act as stupid TCP clients,
 * the interessting stuff happens on the server side.
 */

static void
test_9p__header_too_short1(void)
{
	setcmd("header_too_short1\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__header_too_short2(void)
{
	setcmd("header_too_short2\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__header_too_large(void)
{
	setcmd("header_too_large\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__header_wrong_type(void)
{
	setcmd("header_wrong_type\n");
	TEST_ASSERT_EQUAL_INT(-ENOTSUP, _9pversion(&ctx));
}

static void
test_9p__header_invalid_type(void)
{
	setcmd("header_invalid_type\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__header_tag_mismatch(void)
{
	setcmd("header_tag_mismatch\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__header_type_mismatch(void)
{
	setcmd("header_type_mismatch\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__rversion_success(void)
{
	setcmd("rversion_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pversion(&ctx));
}

static void
test_9p__rversion_unknown(void)
{
	setcmd("rversion_unknown\n");
	TEST_ASSERT_EQUAL_INT(-ENOPROTOOPT, _9pversion(&ctx));
}

static void
test_9p__rversion_msize_too_big(void)
{
	setcmd("rversion_msize_too_big\n");
	TEST_ASSERT_EQUAL_INT(-EMSGSIZE, _9pversion(&ctx));
}

static void
test_9p__rversion_invalid(void)
{
	setcmd("rversion_invalid\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__rversion_invalid_len(void)
{
	setcmd("rversion_invalid_len\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__rversion_version_too_long(void)
{
	setcmd("rversion_version_too_long\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pversion(&ctx));
}

static void
test_9p__rattach_success(void)
{
	_9pfid *fid;

	setcmd("rattach_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pattach(&ctx, &fid, "foo", NULL));

	TEST_ASSERT(fid->fid > 0);
	TEST_ASSERT_EQUAL_INT(1, cntfids(ctx.fids));
}

static void
test_9p__rattach_invalid_len(void)
{
	_9pfid *fid;

	setcmd("rattach_invalid_len\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pattach(&ctx, &fid, "foobar", NULL));
}

static void
test_9p__rstat_success(void)
{
	_9pfid f;
	struct stat st;

	f.fid = 2342;

	setcmd("rstat_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pstat(&ctx, &f, &st));

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
}

static void
test_9p__rwalk_success(void)
{
	_9pfid *f;

	setcmd("rwalk_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&ctx, &f, "foo/bar"));

	TEST_ASSERT_EQUAL_INT(23, f->qid.type);
	TEST_ASSERT_EQUAL_INT(42, f->qid.vers);
	TEST_ASSERT_EQUAL_INT(1337, f->qid.path);

	TEST_ASSERT_EQUAL_INT(1, cntfids(ctx.fids));
}

static void
test_9p__rwalk_rootfid(void)
{
	_9pfid *f;

	setcmd("rwalk_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pwalk(&ctx, &f, "/"));

	TEST_ASSERT(f->fid != _9P_ROOTFID);
	TEST_ASSERT_EQUAL_INT(1, cntfids(ctx.fids));
}

static void
test_9p__rwalk_invalid_len(void)
{
	_9pfid *f;

	setcmd("rwalk_invalid_len\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pwalk(&ctx, &f, "foobar"));
	TEST_ASSERT_EQUAL_INT(0, cntfids(ctx.fids));
}

static void
test_9p__rwalk_path_too_long(void)
{
	_9pfid *f;
	char *path;

	path = "1/2/3/4/5/6/7/8/9/10/11/12/13/14/15/16/17";
	TEST_ASSERT_EQUAL_INT(-ENAMETOOLONG, _9pwalk(&ctx, &f, path));
	TEST_ASSERT_EQUAL_INT(0, cntfids(ctx.fids));
}

static void
test_9p__rwalk_nwqid_too_large(void)
{
	_9pfid *f;

	setcmd("rwalk_nwqid_too_large\n");
	TEST_ASSERT_EQUAL_INT(-EBADMSG, _9pwalk(&ctx, &f, "foo"));
	TEST_ASSERT_EQUAL_INT(0, cntfids(ctx.fids));
}

static void
test_9p__ropen_success(void)
{
	_9pfid f;

	f.fid = 42;

	setcmd("ropen_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9popen(&ctx, &f, 0));
	TEST_ASSERT_EQUAL_INT(1337, f.iounit);
}

static void
test_9p__rcreate_success(void)
{
	_9pfid f;

	f.fid = 4223;

	setcmd("rcreate_success\n");
	TEST_ASSERT_EQUAL_INT(0, _9pcreate(&ctx, &f, "hurrdurr", ORDWR, 0));
	TEST_ASSERT_EQUAL_INT(9001, f.iounit);
}

static void
test_9p__rread_success(void)
{
	_9pfid f;
	ssize_t ret;
	char dest[7];

	setcmd("rread_success\n");

	f.fid = 42;
	f.off = 0;
	f.iounit = 50;

	ret = _9pread(&ctx, &f, dest, 6);
	TEST_ASSERT_EQUAL_INT(6, ret);

	dest[ret] = '\0';
	TEST_ASSERT_EQUAL_STRING("Hello!", (char*)dest);
}

static void
test_9p__rread_with_offset1(void)
{
	_9pfid f;
	ssize_t ret;
	char dest[11];

	// Set command twice because with an iounit of 5
	// we need to send two requests to receive the
	// entire file.
	setcmd("rread_with_offset\n");
	setcmd("rread_with_offset\n");

	f.fid = 23;
	f.off = 0;
	f.iounit = 5;

	ret = _9pread(&ctx, &f, dest, 10);
	TEST_ASSERT_EQUAL_INT(10, ret);

	dest[ret] = '\0';
	TEST_ASSERT_EQUAL_STRING("1234567890", (char*)dest);
}

static void
test_9p__rread_with_offset2(void)
{
	_9pfid f;
	ssize_t ret;
	char dest[6];

	setcmd("rread_with_offset\n");

	f.fid = 42;
	f.off = 0;
	f.iounit = 9999;

	ret = _9pread(&ctx, &f, dest, 5);
	dest[ret] = '\0';

	TEST_ASSERT_EQUAL_INT(5, ret);
	TEST_ASSERT_EQUAL_STRING("12345", (char*)dest);

	setcmd("rread_with_offset\n");

	ret = _9pread(&ctx, &f, dest, 5);
	dest[ret] = '\0';

	TEST_ASSERT_EQUAL_INT(5, ret);
	TEST_ASSERT_EQUAL_STRING("67890", (char*)dest);
}

static void
test_9p__rread_count_zero(void)
{
	_9pfid f;
	char dest[11];

	setcmd("rread_count_zero\n");

	f.fid = 42;
	f.off = 0;
	f.iounit = 1337;

	TEST_ASSERT_EQUAL_INT(0, _9pread(&ctx, &f, dest, 10));
}

static void
test_9p__rread_with_larger_count(void)
{
	_9pfid f;
	ssize_t ret;
	char dest[7];

	setcmd("rread_success\n");

	f.fid = 5;
	f.off = 0;
	f.iounit = 100;

	ret = _9pread(&ctx, &f, dest, 100);
	TEST_ASSERT_EQUAL_INT(6, ret);

	dest[ret] = '\0';
	TEST_ASSERT_EQUAL_STRING("Hello!", (char*)dest);
}

static void
test_9p__rwrite_success(void)
{
	_9pfid f;
	char *str = "hurrdurr";
	size_t l;

	setcmd("rwrite_success\n");

	f.fid = 9002;
	f.off = 0;
	f.iounit = 50;

	l = strlen(str);
	TEST_ASSERT_EQUAL_INT(l, _9pwrite(&ctx, &f, str, l));
}

static void
test_9p__rclunk_success(void)
{
	_9pfid *f;

	setcmd("rclunk_success\n");

	f = fidtbl(ctx.fids, 23, ADD);
	f->fid = 23;

	TEST_ASSERT_EQUAL_INT(0, _9pclunk(&ctx, f));
}

static void
test_9p__rclunk_bad_fid(void)
{
	_9pfid f;

	setcmd("rclunk_success\n");

	f.fid = 42;
	TEST_ASSERT_EQUAL_INT(-EBADF, _9pclunk(&ctx, &f));
}

static void
test_9p__rremove_success(void)
{
	_9pfid *f;

	setcmd("remove_success\n");

	f = fidtbl(ctx.fids, 9, ADD);
	f->fid = 9;

	TEST_ASSERT_EQUAL_INT(0, _9premove(&ctx, f));
}

static void
test_9p__rremove_bad_fid(void)
{
	_9pfid f;

	setcmd("remove_success\n");

	f.fid = 5;
	TEST_ASSERT_EQUAL_INT(-EBADF, _9premove(&ctx, &f));
}

Test*
tests_9p_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9p__header_too_short1),
		new_TestFixture(test_9p__header_too_short2),
		new_TestFixture(test_9p__header_too_large),
		new_TestFixture(test_9p__header_wrong_type),
		new_TestFixture(test_9p__header_invalid_type),
		new_TestFixture(test_9p__header_tag_mismatch),
		new_TestFixture(test_9p__header_type_mismatch),

		new_TestFixture(test_9p__rversion_success),
		new_TestFixture(test_9p__rversion_unknown),
		new_TestFixture(test_9p__rversion_msize_too_big),
		new_TestFixture(test_9p__rversion_invalid),
		new_TestFixture(test_9p__rversion_invalid_len),
		new_TestFixture(test_9p__rversion_version_too_long),

		new_TestFixture(test_9p__rattach_success),
		new_TestFixture(test_9p__rattach_invalid_len),

		new_TestFixture(test_9p__rstat_success),

		new_TestFixture(test_9p__rwalk_success),
		new_TestFixture(test_9p__rwalk_rootfid),
		new_TestFixture(test_9p__rwalk_invalid_len),
		new_TestFixture(test_9p__rwalk_path_too_long),
		new_TestFixture(test_9p__rwalk_nwqid_too_large),

		new_TestFixture(test_9p__ropen_success),

		new_TestFixture(test_9p__rcreate_success),

		new_TestFixture(test_9p__rread_success),
		new_TestFixture(test_9p__rread_with_offset1),
		new_TestFixture(test_9p__rread_with_offset2),
		new_TestFixture(test_9p__rread_count_zero),
		new_TestFixture(test_9p__rread_with_larger_count),

		new_TestFixture(test_9p__rwrite_success),

		new_TestFixture(test_9p__rclunk_success),
		new_TestFixture(test_9p__rclunk_bad_fid),

		new_TestFixture(test_9p__rremove_success),
		new_TestFixture(test_9p__rremove_bad_fid),
	};

	EMB_UNIT_TESTCALLER(_9p_tests, set_up, tear_down, fixtures);
	return (Test*)&_9p_tests;
}

/**@}*/

int
main(void)
{
	char *addr, *cport, *pport;
	sock_tcp_ep_t cr = SOCK_IPV6_EP_ANY;
	sock_tcp_ep_t pr = SOCK_IPV6_EP_ANY;

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	GETENV(addr, "NINERIOT_ADDR");

	if (!ipv6_addr_from_str((ipv6_addr_t*)&cr.addr, addr)
			|| !ipv6_addr_from_str((ipv6_addr_t*)&pr.addr, addr)) {
		fprintf(stderr, "Address '%s' is malformed\n", addr);
		return EXIT_FAILURE;
	}

	GETENV(pport, "NINERIOT_PPORT");
	GETENV(cport, "NINERIOT_CPORT");

	pr.port = atoi(pport);
	cr.port = atoi(cport);

	if (sock_tcp_connect(&csock, &cr, 0, SOCK_FLAGS_REUSE_EP) < 0
			|| sock_tcp_connect(&psock, &pr, 0, SOCK_FLAGS_REUSE_EP) < 0) {
		fprintf(stderr, "Couldn't connect to server\n");
		return EXIT_FAILURE;
	}

	_9pinit(&ctx, recvfn, sendfn);

	TESTS_START();
	TESTS_RUN(tests_9putil_tests());
	TESTS_RUN(tests_9p_tests());
	TESTS_END();

	sock_tcp_disconnect(&csock);
	sock_tcp_disconnect(&psock);

	return EXIT_SUCCESS;
}
