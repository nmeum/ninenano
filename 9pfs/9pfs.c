#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "9p.h"
#include "vfs.h"
#include "9pfs.h"
#include "9util.h"

#define ENABLE_DEBUG (0)
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
 * @defgroup _9pfs_fs_ops File system operations.
 *
 * @{
 */

static int
_9pfs_mount(vfs_mount_t *mountp)
{
	int r;
	_9pfid *f;
	_9pfs *fs;

	fs = mountp->private_data;

	mutex_init(&fs->mtx);
	if ((r = _9pversion(&fs->ctx)))
		return r;
	if ((r = _9pattach(&fs->ctx, &f, fs->uname, fs->aname)))
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
	_9pfs *fs;
	int r;

	r = 0;
	fs = mountp->private_data;

	mutex_lock(&fs->mtx);
	if (_9pwalk(&fs->ctx, &f, (char*)name)) {
		r = -ENOENT;
		goto ret;
	}
	if (_9premove(&fs->ctx, f)) {
		r = -EACCES;
		goto ret;
	}

ret:
	mutex_unlock(&fs->mtx);
	return r;
}

static int
_9pfs_mkdir(vfs_mount_t *mountp, const char *name, mode_t mode)
{
	_9pfid *f;
	char buf[VFS_NAME_MAX + 1];
	char *dname, *bname;
	_9pfs *fs;
	int r;

	r = 0;
	fs = mountp->private_data;

	mutex_lock(&fs->mtx);
	if (!_9pwalk(&fs->ctx, &f, (char*)name)) {
		_9pclunk(&fs->ctx, f);
		r = -EEXIST;
		goto ret;
	}

	bname = breakpath(buf, sizeof(buf), name, &dname);
	DEBUG("Creating directory '%s' in directory '%s'\n", bname, dname);

	if (_9pwalk(&fs->ctx, &f, dname)) {
		r = -EACCES;
		goto ret;
	}

	mode &= 0777;
	mode |= DMDIR;

	if (_9pcreate(&fs->ctx, f, bname, mode, OREAD)) {
		_9pclunk(&fs->ctx, f);
		r = -EACCES;
		goto ret;
	}

	_9pclunk(&fs->ctx, f);

ret:
	mutex_unlock(&fs->mtx);
	return r;
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
	_9pfs *fs;
	int r;

	fs = mountp->private_data;
	r = 0;

	mutex_lock(&fs->mtx);
	if (_9pwalk(&fs->ctx, &f, (char*)name)) {
		r = -ENOENT;
		goto ret;
	}

	if (_9pstat(&fs->ctx, f, buf)) {
		_9pclunk(&fs->ctx, f);
		r = -EACCES;
		goto ret;
	}

	_9pclunk(&fs->ctx, f);

ret:
	mutex_unlock(&fs->mtx);
	return r;
}

/**@}*/

/**
 * @defgroup _9pfs_file_ops File operations.
 *
 * @{
 */

static int
_9pfs_close(vfs_file_t *filp)
{
	_9pfid *f;
	_9pfs *fs;

	f = filp->private_data.ptr;
	fs = filp->mp->private_data;

	mutex_lock(&fs->mtx);
	if (_9pclunk(&fs->ctx, f) == -EBADF) {
		mutex_unlock(&fs->mtx);
		return -EBADF;
	}
	mutex_unlock(&fs->mtx);

	return 0;
}

static int
_9pfs_fstat(vfs_file_t *filp, struct stat *buf)
{
	_9pfid *f;
	_9pfs *fs;

	f = filp->private_data.ptr;
	fs = filp->mp->private_data;

	mutex_lock(&fs->mtx);
	if (_9pstat(&fs->ctx, f, buf)) {
		mutex_unlock(&fs->mtx);
		return -EACCES;
	}
	mutex_unlock(&fs->mtx);

	return 0;
}

static off_t _9pfs_lseek(vfs_file_t *filp, off_t off, int whence)
{
	_9pfid *f;
	_9pfs *fs;
	struct stat st;

	f = filp->private_data.ptr;
	fs = filp->mp->private_data;

	switch (whence) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			off += f->off;
			break;
		case SEEK_END:
			mutex_lock(&fs->mtx);
			if (_9pstat(&fs->ctx, f, &st)) {
				mutex_unlock(&fs->mtx);
				return -EINVAL;
			}
			mutex_unlock(&fs->mtx);

			off += st.st_size;
			break;
		default:
			return -EINVAL;
	}

	if (off < 0)
		return -EINVAL;

	mutex_lock(&fs->mtx);
	f->off = off;
	mutex_unlock(&fs->mtx);

	return off;
}

static int
_9pfs_open(vfs_file_t *filp, const char *name, int flags, mode_t mode, const char *abs_path)
{
	int r, fl;
	_9pfid *f;
	_9pfs *fs;
	char buf[VFS_NAME_MAX + 1];
	char *bname, *dname;

	(void)abs_path;

	r = 0;
	fs = filp->mp->private_data;

	/* Convert the mode. This assumes that OREAD == O_RDONLY,
	 * O_WRONLY == OWRITE and O_RDWR == ORDWR which should always be
	 * the case on RIOT. */
	fl = flags & O_ACCMODE;
	if (flags & O_TRUNC)
		fl |= OTRUNC;

	mutex_lock(&fs->mtx);
	if (!_9pwalk(&fs->ctx, &f, (char*)name)) {
		if (_9popen(&fs->ctx, f, fl)) {
			_9pclunk(&fs->ctx, f);
			r = -EACCES;
			goto ret;
		}

		filp->private_data.ptr = f;
		goto ret;
	} else if (flags & O_CREAT) {
		bname = breakpath(buf, sizeof(buf), name, &dname);
		if (_9pwalk(&fs->ctx, &f, dname)) {
			r = -ENOENT;
			goto ret;
		}

		if (_9pcreate(&fs->ctx, f, bname, mode, fl)) {
			_9pclunk(&fs->ctx, f);
			r = -EACCES;
			goto ret;
		}

		filp->private_data.ptr = f;
		goto ret;
	} else {
		r = -ENOENT;
		goto ret;
	}

ret:
	mutex_unlock(&fs->mtx);
	return r;
}

static ssize_t
_9pfs_read(vfs_file_t *filp, void *dest, size_t nbytes)
{
	ssize_t ret;
	_9pfid *f;
	_9pfs *fs;

	f = filp->private_data.ptr;
	fs = filp->mp->private_data;

	mutex_lock(&fs->mtx);
	if ((ret = _9pread(&fs->ctx, f, dest, nbytes)) < 0) {
		mutex_unlock(&fs->mtx);
		return -EIO;
	}
	mutex_unlock(&fs->mtx);

	return ret;
}

static ssize_t
_9pfs_write(vfs_file_t *filp, const void *src, size_t nbytes)
{
	ssize_t ret;
	_9pfid *f;
	_9pfs *fs;

	f = filp->private_data.ptr;
	fs = filp->mp->private_data;

	mutex_lock(&fs->mtx);
	if ((ret = _9pwrite(&fs->ctx, f, (void*)src, nbytes)) < 0) {
		mutex_unlock(&fs->mtx);
		return -EIO;
	}
	mutex_unlock(&fs->mtx);

	return ret;
}

/**@}*/

/**
 * @defgroup _9pfs_dir_ops Directory operations.
 *
 * @{
 */

static int
_9pfs_opendir(vfs_DIR *dirp, const char *dirname, const char *abs_path)
{
	_9pfid *f;
	_9pfs *fs;
	int r;

	(void)abs_path;

	r = 0;
	fs = dirp->mp->private_data;

	mutex_lock(&fs->mtx);
	if (_9pwalk(&fs->ctx, &f, (char*)dirname)) {
		r = -ENOENT;
		goto ret;
	}

	if (_9popen(&fs->ctx, f, OREAD)) {
		_9pclunk(&fs->ctx, f);
		r = -EACCES;
		goto ret;
	}

	if (!(f->qid.type & QTDIR)) {
		_9pclunk(&fs->ctx, f);
		r = -ENOTDIR;
		goto ret;
	}

	dirp->private_data.ptr = f;

ret:
	mutex_unlock(&fs->mtx);
	return r;
}

static int
_9pfs_readdir(vfs_DIR *dirp, vfs_dirent_t *entry)
{
	ssize_t n;
	_9pfid *f;
	_9ppkt pkt;
	_9pfs *fs;
	char dest[_9P_MINSTSIZ + VFS_NAME_MAX + 1];
	int r;

	r = 0;
	f = dirp->private_data.ptr;
	fs = dirp->mp->private_data;

	mutex_lock(&fs->mtx);
	if ((n = _9pread(&fs->ctx, f, dest, sizeof(dest))) < 0) {
		r = -EIO;
		goto ret;
	} else if (n == 0) {
		goto ret;
	}

	pkt.len = n;
	pkt.buf = (unsigned char*)dest;

	/* Skip all the information we don't need. */
	advbuf(&pkt, 2 * BIT16SZ + BIT32SZ + _9P_QIDSIZ + 3 * BIT32SZ + BIT64SZ);
	if (hstring(entry->d_name, sizeof(entry->d_name), &pkt)) {
		r = -EIO;
		goto ret;
	}

	entry->d_ino = 0; /* XXX */
	r = 1;

ret:
	mutex_unlock(&fs->mtx);
	return r;
}

static int
_9pfs_closedir(vfs_DIR *dirp)
{
	_9pfid *f;
	_9pfs *fs;

	f = dirp->private_data.ptr;
	fs = dirp->mp->private_data;

	mutex_lock(&fs->mtx);
	if (_9pclunk(&fs->ctx, f) == -EBADF) {
		mutex_unlock(&fs->mtx);
		return -EBADF;
	}
	mutex_unlock(&fs->mtx);

	return 0;
}

/**@}*/

/**
 * @defgroup fs Struct definitions.
 *
 * @{
 */

static const vfs_file_system_ops_t _9pfs_fs_ops = {
	.mount = _9pfs_mount,
	.umount = _9pfs_umount,
	.rename = NULL, /* TODO */
	.unlink = _9pfs_unlink,
	.mkdir = _9pfs_mkdir,
	.rmdir = _9pfs_rmdir,
	.stat = _9pfs_stat,
	.statvfs = NULL,
	.fstatvfs = NULL, /* TODO */
};

static const vfs_file_ops_t _9pfs_file_ops = {
	.close = _9pfs_close,
	.fcntl = NULL,
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
