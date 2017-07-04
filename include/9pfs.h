#ifndef NINEPFS_H
#define NINEPFS_H

#include "9p.h"
#include "vfs.h"
#include "mutex.h"

extern const vfs_file_system_t _9p_file_system;

/**
 * 9P file system superblock.
 */
typedef struct {
	/**
	 * 9P context for the underlying connection.
	 */
	_9pctx ctx;

	/**
	 * Mutex used to synchronize access to the connection context.
	 */
	mutex_t mtx;

	/**
	 * User identification.
	 */
	char *uname;

	/**
	 * File tree to access (can be NULL).
	 */
	char *aname;
} _9pfs;

#endif
