#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "9p.h"
#include "vfs.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

/**
 * Split a given path into the directory and file portition.
 *
 * @param buf Pointer to a temporary buffer used to create a copy of the
 *	path argument in order to prevent modification of the latter.
 * @param bufsiz Size of the temporary buffer.
 * @param path Pointer to the path which should be split.
 * @param dname Pointer to a memory location which should be set to the
 * 	address of the directory portion.
 * @return Pointer to the file portion of the name.
 */
static char*
breakpath(char *buf, size_t bufsiz, const char *path, char **dname)
{
	char *bname;

	strncpy(buf, path, bufsiz);
	*dname = buf;

	bname = strrchr(buf, '/');
	*bname++ = '\0';
	return bname;
}

/**
 * @defgroup File system operations.
 *
 * @{
 */

static int
_9pfs_mount(vfs_mount_t *mountp)
{
	int r;
	_9pfid *f;
	_9pctx *ctx;

	ctx = mountp->private_data;

	_9pinit(ctx);
	if ((r = _9pversion(ctx)))
		return r;
	if ((r = _9pattach(ctx, &f, "foobar", NULL)))
		return r;

	(void)f;
	return 0;
}

static int
_9pfs_umount(vfs_mount_t *mountp)
{
	(void)mountp;

	return 0;
}

static int
_9pfs_unlink(vfs_mount_t *mountp, const char *name)
{
	_9pfid *f;
	_9pctx *ctx;

	ctx = mountp->private_data;

	if (_9pwalk(ctx, &f, (char*)name))
		return -ENOENT;
	if (_9premove(ctx, f))
		return -EACCES;

	return 0;
}

static int
_9pfs_mkdir(vfs_mount_t *mountp, const char *name, mode_t mode)
{
	_9pfid *f;
	char buf[VFS_NAME_MAX + 1];
	char *dname, *bname;
	_9pctx *ctx;

	ctx = mountp->private_data;

	if (!_9pwalk(ctx, &f, (char*)name)) {
		_9pclunk(ctx, f);
		return -EEXIST;
	}

	bname = breakpath(buf, sizeof(buf), name, &dname);
	DEBUG("Creating directory '%s' in directory '%s'\n", bname, dname);

	if (_9pwalk(ctx, &f, dname))
		return -EACCES;

	mode &= 0777;
	mode |= DMDIR;

	if (_9pcreate(ctx, f, bname, mode, OREAD))
		return -EACCES;

	_9pclunk(ctx, f);
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
	_9pctx *ctx;

	ctx = mountp->private_data;

	if (_9pwalk(ctx, &f, (char*)name))
		return -ENOENT;
	if (_9pstat(ctx, f, buf))
		return -EACCES;

	_9pclunk(ctx, f);
	return 0;
}

/**@}*/

/**
 * @defgroup File operations.
 *
 * @{
 */

static int
_9pfs_close(vfs_file_t *filp)
{
	_9pfid *f;
	_9pctx *ctx;

	f = filp->private_data.ptr;
	ctx = filp->mp->private_data;

	if (_9pclunk(ctx, f) == -EBADF)
		return -EBADF;

	return 0;
}

static int
_9pfs_fstat(vfs_file_t *filp, struct stat *buf)
{
	_9pfid *f;
	_9pctx *ctx;

	f = filp->private_data.ptr;
	ctx = filp->mp->private_data;

	if (_9pstat(ctx, f, buf))
		return -EACCES;

	return 0;
}

static off_t _9pfs_lseek(vfs_file_t *filp, off_t off, int whence)
{
	_9pfid *f;
	_9pctx *ctx;
	struct stat st;

	f = filp->private_data.ptr;
	ctx = filp->mp->private_data;

	switch (whence) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			off += f->off;
			break;
		case SEEK_END:
			if (_9pstat(ctx, f, &st))
				return -EINVAL;

			off += st.st_size;
			break;
		default:
			return -EINVAL;
	}

	if (off < 0)
		return -EINVAL;

	f->off = off;
	return off;
}

static int
_9pfs_open(vfs_file_t *filp, const char *name, int flags, mode_t mode, const char *abs_path)
{
	int fl;
	_9pfid *f;
	_9pctx *ctx;
	char buf[VFS_NAME_MAX + 1];
	char *bname, *dname;

	(void)abs_path;

	ctx = filp->mp->private_data;

	/* Convert the mode. This assumes that OREAD == O_RDONLY,
	 * O_WRONLY == OWRITE and O_RDWR == ORDWR which should always be
	 * the case on RIOT. */
	fl = flags & O_ACCMODE;
	if (flags & O_TRUNC)
		fl |= OTRUNC;

	if (!_9pwalk(ctx, &f, (char*)name)) {
		if (_9popen(ctx, f, fl)) {
			_9pclunk(ctx, f);
			return -EACCES;
		}

		filp->private_data.ptr = f;
		return 0;
	} else if (flags & O_CREAT) {
		bname = breakpath(buf, sizeof(buf), name, &dname);
		if (_9pwalk(ctx, &f, dname))
			return -ENOENT;

		if (_9pcreate(ctx, f, bname, mode, flags)) {
			_9pclunk(ctx, f);
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
	_9pctx *ctx;

	f = filp->private_data.ptr;
	ctx = filp->mp->private_data;

	if ((ret = _9pread(ctx, f, dest, nbytes)) < 0)
		return -EIO;

	return ret;
}

static ssize_t
_9pfs_write(vfs_file_t *filp, const void *src, size_t nbytes)
{
	ssize_t ret;
	_9pfid *f;
	_9pctx *ctx;

	f = filp->private_data.ptr;
	ctx = filp->mp->private_data;

	if ((ret = _9pwrite(ctx, f, (void*)src, nbytes)) < 0)
		return -EIO;

	return ret;
}

/**@}*/

/**
 * @defgroup Directory operations.
 *
 * @{
 */

static int
_9pfs_opendir(vfs_DIR *dirp, const char *dirname, const char *abs_path)
{
	_9pfid *f;
	_9pctx *ctx;

	(void)abs_path;

	ctx = dirp->mp->private_data;

	if (_9pwalk(ctx, &f, (char*)dirname))
		return -ENOENT;

	if (_9popen(ctx, f, OREAD)) {
		_9pclunk(ctx, f);
		return -EACCES;
	}

	if (!(f->qid.type & QTDIR)) {
		_9pclunk(ctx, f);
		return -ENOTDIR;
	}

	dirp->private_data.ptr = f;
	return 0;
}

static int
_9pfs_readdir(vfs_DIR *dirp, vfs_dirent_t *entry)
{
	ssize_t n;
	_9pfid *f;
	_9ppkt pkt;
	_9pctx *ctx;
	char dest[_9P_STATSIZ + VFS_NAME_MAX + 1];

	f = dirp->private_data.ptr;
	ctx = dirp->mp->private_data;

	if ((n = _9pread(ctx, f, dest, sizeof(dest))) < 0)
		return -EIO;
	else if (n == 0)
		return 0;

	pkt.len = n;
	pkt.buf = (unsigned char*)dest;

	/* Skip all the information we don't need. */
	advbuf(&pkt, 2 * BIT16SZ + BIT32SZ + _9P_QIDSIZ + 3 * BIT32SZ + BIT64SZ);
	if (hstring(entry->d_name, sizeof(entry->d_name), &pkt))
		return -EIO;

	entry->d_ino = 0; /* XXX */
	return 1;
}

static int
_9pfs_closedir(vfs_DIR *dirp)
{
	_9pfid *f;
	_9pctx *ctx;

	f = dirp->private_data.ptr;
	ctx = dirp->mp->private_data;

	if (_9pclunk(ctx, f) == -EBADF)
		return -EBADF;

	return 0;
}

/**@}*/

/**
 * @defgroup Struct definitions.
 *
 * @{
 */

static const vfs_file_system_ops_t _9pfs_fs_ops = {
	.mount = _9pfs_mount,
	.umount = _9pfs_umount,
	.unlink = _9pfs_unlink,
	.mkdir = _9pfs_mkdir,
	.rmdir = _9pfs_rmdir,
	.stat = _9pfs_stat,
	.statvfs = NULL,
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

/**@}*/
