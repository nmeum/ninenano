#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "9p.h"
#include "vfs.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

/* File system operations */
static int _9pfs_mount(vfs_mount_t *mountp);
static int _9pfs_umount(vfs_mount_t *mountp);
static int _9pfs_unlink(vfs_mount_t *mountp, const char *name);
static int _9pfs_mkdir(vfs_mount_t *mountp, const char *name, mode_t mode);
static int _9pfs_rmdir(vfs_mount_t *mountp, const char *name);
static int _9pfs_stat(vfs_mount_t *mountp, const char *restrict name, struct stat *restrict buf);
/* static int _9pfs_statvfs(vfs_mount_t *mountp, const char *restrict path, struct statvfs *restrict buf); */

/* File operations */
static int _9pfs_close(vfs_file_t *filp);
static int _9pfs_fstat(vfs_file_t *filp, struct stat *buf);
static off_t _9pfs_lseek(vfs_file_t *filp, off_t off, int whence);
static int _9pfs_open(vfs_file_t *filp, const char *name, int flags, mode_t mode, const char *abs_path);
static ssize_t _9pfs_read(vfs_file_t *filp, void *dest, size_t nbytes);
static ssize_t _9pfs_write(vfs_file_t *filp, const void *src, size_t nbytes);

/* Directory operations */
static int _9pfs_opendir(vfs_DIR *dirp, const char *dirname, const char *abs_path);
static int _9pfs_readdir(vfs_DIR *dirp, vfs_dirent_t *entry);
static int _9pfs_closedir(vfs_DIR *dirp);

static const vfs_file_system_ops_t _9pfs_fs_ops = {
	.mount = _9pfs_mount,
	.umount = _9pfs_umount,
	.unlink = _9pfs_unlink,
	.mkdir = _9pfs_mkdir,
	.rmdir = _9pfs_rmdir,
	.stat = _9pfs_stat,
};

static const vfs_file_ops_t _9pfs_file_ops = {
	.close = _9pfs_close,
	.fstat = _9pfs_fstat,
	.lseek = _9pfs_lseek,
	.open = _9pfs_open,
	.read = _9pfs_read,
	.write = _9pfs_write,
};

static const vfs_dir_ops_t _9pfs_dir_ops = {
	.opendir = _9pfs_opendir,
	.readdir = _9pfs_readdir,
	.closedir = _9pfs_closedir,
};

const vfs_file_system_t _9p_file_system = {
	.f_op = &_9pfs_file_ops,
	.fs_op = &_9pfs_fs_ops,
	.d_op = &_9pfs_dir_ops,
};

static char*
breakpath(char *buf, const char *path, char **dname)
{
	char *bname;

	strncpy(buf, path, VFS_NAME_MAX + 1);
	*dname = buf;

	bname = strrchr(buf, '/');
	*bname++ = '\0';
	return bname;
}

static int
_9pfs_mount(vfs_mount_t *mountp)
{
	int r;
	_9pfid *f;
	sock_tcp_ep_t *ep;

	if (!mountp->private_data)
		return -EINVAL;
	ep = (sock_tcp_ep_t*)mountp->private_data;

	if ((r = _9pinit(ep)))
		return r;
	if ((r = _9pversion()))
		return r;
	if ((r = _9pattach(&f, "foobar", NULL)))
		return r;

	(void)f;
	return 0;
}

static int
_9pfs_umount(vfs_mount_t *mountp)
{
	(void)mountp;

	_9pclose();
	return 0;
}

static int
_9pfs_unlink(vfs_mount_t *mountp, const char *name)
{
	_9pfid *f;

	(void)mountp;

	if (_9pwalk(&f, (char*)name))
		return -ENOENT;
	if (_9premove(f))
		return -EACCES;

	return 0;
}

static int
_9pfs_mkdir(vfs_mount_t *mountp, const char *name, mode_t mode)
{
	_9pfid *f;
	char buf[VFS_NAME_MAX + 1];
	char *dname, *bname;

	(void)mountp;

	if (!_9pwalk(&f, (char*)name)) {
		_9pclunk(f);
		return -EEXIST;
	}

	bname = breakpath(buf, name, &dname);
	DEBUG("Creating directory '%s' in directory '%s'\n", bname, dname);

	if (_9pwalk(&f, dname))
		return -EACCES;

	mode &= 0777;
	mode |= DMDIR;

	if (_9pcreate(f, bname, mode, OREAD))
		return -EACCES;

	_9pclunk(f);
	return 0;
}

static int
_9pfs_rmdir(vfs_mount_t *mountp, const char *name)
{
	return _9pfs_unlink(mountp, name);
}

static int
_9pfs_stat(vfs_mount_t *mountp, const char *restrict name, struct stat *restrict buf)
{
	_9pfid *f;

	(void)mountp;

	if (_9pwalk(&f, (char*)name))
		return -ENOENT;
	if (_9pstat(f, buf))
		return -EACCES;

	_9pclunk(f);
	return 0;
}

static int
_9pfs_close(vfs_file_t *filp)
{
	_9pfid *f;

	f = filp->private_data.ptr;

	if (_9pclunk(f) == -EBADF)
		return -EBADF;

	/* From clunk(5):
	 *   Even if the clunk returns an error, the fid is no longer
	 *   valid.
	 */
	filp->private_data.ptr = NULL;
	return 0;
}

static int
_9pfs_fstat(vfs_file_t *filp, struct stat *buf)
{
	_9pfid *f;

	f = filp->private_data.ptr;

	if (_9pstat(f, buf))
		return -EACCES;

	return 0;
}

static off_t _9pfs_lseek(vfs_file_t *filp, off_t off, int whence)
{
	_9pfid *f;
	struct stat st;

	f = filp->private_data.ptr;

	switch (whence) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			off += filp->pos;
			break;
		case SEEK_END:
			if (_9pstat(f, &st))
				return -EINVAL;

			off += st.st_size;
			break;
		default:
			return -EINVAL;
	}

	if (off < 0)
		return -EINVAL;

	filp->pos = off;
	return off;
}

static int
_9pfs_open(vfs_file_t *filp, const char *name, int flags, mode_t mode, const char *abs_path)
{
	int fl;
	_9pfid *f;
	char buf[VFS_NAME_MAX + 1];
	char *bname, *dname;

	(void)abs_path;

	/* Convert the mode. This assumes that OREAD == O_RDONLY,
	 * O_WRONLY == OWRITE and O_RDWR == ORDWR which should always be
	 * the case on RIOT. */
	fl = flags & O_ACCMODE;
	if (flags & O_TRUNC)
		fl |= OTRUNC;

	if (!_9pwalk(&f, (char*)name)) {
		if (_9popen(f, fl)) {
			_9pclunk(f);
			return -EACCES;
		}

		filp->private_data.ptr = f;
		return 0;
	} else if (flags & O_CREAT) {
		bname = breakpath(buf, name, &dname);
		if (_9pwalk(&f, dname))
			return -ENOENT;

		if (_9pcreate(f, bname, mode, flags)) {
			_9pclunk(f);
			return -EACCES;
		}

		filp->private_data.ptr = f;
		return 0;
	} else {
		return -ENOENT;
	}
}

static ssize_t
_9pfs_read(vfs_file_t *filp, void *dest, size_t nbytes)
{
	ssize_t ret;
	_9pfid *f;

	f = filp->private_data.ptr;

	if ((ret = _9pread(f, dest, filp->pos, nbytes)) < 0)
		return -EIO;
	filp->pos += ret;

	return ret;
}

static ssize_t
_9pfs_write(vfs_file_t *filp, const void *src, size_t nbytes)
{
	ssize_t ret;
	_9pfid *f;

	f = filp->private_data.ptr;

	if ((ret = _9pwrite(f, (void*)src, filp->pos, nbytes)) < 0)
		return -EIO;
	filp->pos += ret;

	return ret;
}

static int
_9pfs_opendir(vfs_DIR *dirp, const char *dirname, const char *abs_path)
{
	_9pfid *f;

	(void)abs_path;

	if (_9pwalk(&f, (char*)dirname))
		return -ENOENT;

	if (_9popen(f, OREAD)) {
		_9pclunk(f);
		return -EACCES;
	}

	if (!(f->qid.type & QTDIR)) {
		_9pclunk(f);
		return -ENOTDIR;
	}

	dirp->private_data.ptr = f;
	return 0;
}

static int
_9pfs_readdir(vfs_DIR *dirp, vfs_dirent_t *entry)
{
	_9pfid *f;
	_9ppkt pkt;
	ssize_t n;
	char dest[_9P_STATSIZ + VFS_NAME_MAX + 1];

	f = dirp->private_data.ptr;

	if ((n = _9pread(f, dest, 0, sizeof(dest))) < 0)
		return -EIO;
	else if (n == 0)
		return 1;

	pkt.len = n;
	pkt.buf = (uint8_t*)dest;

	/* Skip all the information we don't need. */
	advbuf(&pkt, 2 * BIT16SZ + BIT32SZ + _9P_QIDSIZ + 3 * BIT32SZ + BIT64SZ);
	if (hstring(entry->d_name, sizeof(entry->d_name), &pkt))
		return -EIO;

	entry->d_ino = 0; /* TODO */
	return 0;
}

static int
_9pfs_closedir(vfs_DIR *dirp)
{
	_9pfid *f;

	f = dirp->private_data.ptr;

	if (_9pclunk(f) == -EBADF)
		return -EBADF;

	dirp->private_data.ptr = NULL;
	return 0;
}
