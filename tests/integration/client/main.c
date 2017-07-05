#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "9p.h"
#include "vfs.h"
#include "9pfs.h"
#include "xtimer.h"

#include "lwip.h"
#include "lwip/netif.h"

#include "net/af.h"
#include "net/ipv6/addr.h"
#include "net/sock/tcp.h"

#include "embUnit.h"
#include "../../util.h"

/**
 * 9P file system superblock.
 */
static _9pfs fs;

/**
 * Mount point where the 9pfs is mounted.
 */
static vfs_mount_t mountp;

static void
set_up(void)
{
	int ret;

	mountp.mount_point = "/mnt";
	mountp.fs = &_9p_file_system;
	mountp.private_data = &fs;

	if ((ret = vfs_mount(&mountp)))
		fprintf(stderr, "vfs_mount failed: %d\n", ret);
}

static void
tear_down(void)
{
	int ret;

	if ((ret = vfs_umount(&mountp)))
		fprintf(stderr, "vfs_umount failed: %d\n", ret);
}

static void
test_9pfs__create_and_remove_directory(void)
{
	struct stat st;

	TEST_ASSERT_EQUAL_INT(0, vfs_mkdir("/mnt/foo/wtf", S_IRUSR|S_IWUSR));
	TEST_ASSERT_EQUAL_INT(0, vfs_rmdir("/mnt/foo/wtf"));

	TEST_ASSERT(vfs_stat("/mnt/foo/wtf", &st) != 0);
	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

static void
test_9pfs__read(void)
{
	int fd;
	ssize_t n;
	char dest[BUFSIZ];

	fd = vfs_open("/mnt/foo/bar/hello", OREAD, S_IRUSR|S_IWUSR);
	TEST_ASSERT(fd >= 0);

	n = vfs_read(fd, dest, BUFSIZ - 1);
	TEST_ASSERT_EQUAL_INT(13, n);

	dest[n] = '\0';
	TEST_ASSERT_EQUAL_STRING("Hello World!\n", (char*)dest);

	TEST_ASSERT_EQUAL_INT(0, vfs_close(fd));
}

static void
test_9pfs__lseek_and_read(void)
{
	int fd;
	ssize_t n;
	char dest[BUFSIZ];

	fd = vfs_open("/mnt/foo/bar/hello", OREAD, S_IRUSR|S_IWUSR);
	TEST_ASSERT(fd >= 0);

	TEST_ASSERT_EQUAL_INT(6, vfs_lseek(fd, 6, SEEK_SET));

	n = vfs_read(fd, dest, BUFSIZ - 1);
	TEST_ASSERT_EQUAL_INT(7, n);

	dest[n] = '\0';
	TEST_ASSERT_EQUAL_STRING("World!\n", (char*)dest);

	TEST_ASSERT_EQUAL_INT(0, vfs_close(fd));
}

static void
test_9pfs__create_and_delete(void)
{
	int fd;
	struct stat buf;

	fd = vfs_open("/mnt/foo/falafel", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	TEST_ASSERT(fd >= 0);

	TEST_ASSERT_EQUAL_INT(0, vfs_close(fd));
	TEST_ASSERT_EQUAL_INT(0, vfs_unlink("/mnt/foo/falafel"));

	TEST_ASSERT(vfs_stat("/mnt/foo/falafel", &buf) < 0);
}

static void
test_9pfs__write_lseek_and_read(void)
{
	int fd;
	ssize_t n;
	char *str = "foobar";
	char dest[7];

	fd = vfs_open("/mnt/writeme", O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
	TEST_ASSERT(fd >= 0);

	TEST_ASSERT_EQUAL_INT(2, cntfids(fs.ctx.fids));

	n = vfs_write(fd, str, 6);
	TEST_ASSERT_EQUAL_INT(6, n);

	TEST_ASSERT_EQUAL_INT(0, vfs_lseek(fd, 0, SEEK_SET));

	n = vfs_read(fd, dest, 6);
	TEST_ASSERT_EQUAL_INT(6, n);

	dest[6] = '\0';
	TEST_ASSERT_EQUAL_STRING(str, (char*)dest);

	TEST_ASSERT_EQUAL_INT(0, vfs_close(fd));
	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

static void
test_9pfs__opendir_and_closedir(void)
{
	vfs_DIR dirp;

	TEST_ASSERT_EQUAL_INT(0, vfs_opendir(&dirp, "/mnt/foo"));
	TEST_ASSERT_EQUAL_INT(2, cntfids(fs.ctx.fids));
	TEST_ASSERT_EQUAL_INT(0, vfs_closedir(&dirp));

	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

static void
test_9pfs__opendir_file(void)
{
	vfs_DIR dirp;

	TEST_ASSERT_EQUAL_INT(-ENOTDIR,
		vfs_opendir(&dirp, "/mnt/foo/bar/hello"));
	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

static void
test_9pfs__readdir_single_entry(void)
{
	vfs_DIR dirp;
	vfs_dirent_t entry;

	TEST_ASSERT_EQUAL_INT(0, vfs_opendir(&dirp, "/mnt/foo"));
	TEST_ASSERT_EQUAL_INT(1, vfs_readdir(&dirp, &entry));
	TEST_ASSERT_EQUAL_STRING("bar", (char*)entry.d_name);
	TEST_ASSERT_EQUAL_INT(0, vfs_readdir(&dirp, &entry));

	TEST_ASSERT_EQUAL_INT(0, vfs_closedir(&dirp));
	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

static void
test_9pfs__readdir_multiple_entries(void)
{
	int i;
	vfs_DIR dirp;
	vfs_dirent_t entry;
	char dirname[VFS_NAME_MAX + 1];

	TEST_ASSERT_EQUAL_INT(0, vfs_opendir(&dirp, "/mnt/dirs"));
	for (i = 1; i <= 5; i++) {
		TEST_ASSERT_EQUAL_INT(1, vfs_readdir(&dirp, &entry));
		snprintf(dirname, sizeof(dirname), "%d", i);
		TEST_ASSERT_EQUAL_STRING((char*)dirname, (char*)entry.d_name);
	}
	TEST_ASSERT_EQUAL_INT(0, vfs_readdir(&dirp, &entry));

	TEST_ASSERT_EQUAL_INT(0, vfs_closedir(&dirp));
	TEST_ASSERT_EQUAL_INT(1, cntfids(fs.ctx.fids));
}

Test*
tests_9pfs_tests(void)
{
	EMB_UNIT_TESTFIXTURES(fixtures) {
		new_TestFixture(test_9pfs__read),
		new_TestFixture(test_9pfs__lseek_and_read),
		new_TestFixture(test_9pfs__create_and_delete),
		new_TestFixture(test_9pfs__write_lseek_and_read),
		new_TestFixture(test_9pfs__create_and_remove_directory),
		new_TestFixture(test_9pfs__opendir_and_closedir),
		new_TestFixture(test_9pfs__opendir_file),
		new_TestFixture(test_9pfs__readdir_single_entry),
		new_TestFixture(test_9pfs__readdir_multiple_entries),
	};

	EMB_UNIT_TESTCALLER(_9pfs_tests, set_up, tear_down, fixtures);
	return (Test*)&_9pfs_tests;
}

int
main(void)
{
	char *addr, *port;
	sock_tcp_ep_t remote = SOCK_IPV6_EP_ANY;

	GETENV(addr, "NINERIOT_ADDR");
	GETENV(port, "NINERIOT_PPORT");

	remote.port = atoi(port);
	if (!ipv6_addr_from_str((ipv6_addr_t *)&remote.addr, addr)) {
		fprintf(stderr, "Address '%s' is malformed\n", addr);
		return EXIT_FAILURE;
	}

	puts("Waiting for address autoconfiguration...");
	xtimer_sleep(3);

	if (sock_tcp_connect(&psock, &remote, 0, SOCK_FLAGS_REUSE_EP) < 0) {
		fprintf(stderr, "Couldn't connect to server\n");
		return EXIT_FAILURE;
	}

	fs.uname = "glenda";
	fs.aname = NULL;

	_9pinit(&fs.ctx, recvfn, sendfn);

	TESTS_START();
	TESTS_RUN(tests_9pfs_tests());
	TESTS_END();

	sock_tcp_disconnect(&psock);
	return EXIT_SUCCESS;
}
