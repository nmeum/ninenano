#ifndef NINEP_H
#define NINEP_H

#include <stdint.h>
#include <stddef.h>

#include <sys/stat.h>
#include <sys/types.h>

/**
 * 9P version implemented by this library.
 *
 * From version(5):
 *   Currently, the only defined version is the 6 characters `9P2000`.
 */
#define _9P_VERSION "9P2000"

/**
 * From version(5):
 *   The client suggests a maximum message size, msize, that is the
 *   maximum length, in bytes, it will ever generate or expect to
 *   receive in a single 9P message.
 */
#ifndef _9P_MSIZE
  #define _9P_MSIZE 1024
#endif

/**
 * From intro(5):
 *   Most T-messages contain a fid, a 32-bit unsigned integer that the
 *   client uses to identify a ``current file'' on the server. Fids are
 *   somewhat like file descriptors in a user process, [...] all files
 *   being manipulated by the operating system - are identified by fids.
 *
 * From intro(5):
 *   Replies (R-messages) to auth, attach, walk, open, and create
 *   requests convey a qid field back to the client. The qid represents
 *   the server's unique identification for the file being accessed: two
 *   files on the same server hierarchy are the same if and only if
 *   their qids are the same.
 *
 * Fids need to be mapped to qids, this is achieved using a primitive
 * hash table. The amount of maximum entries in this table can be
 * tweaked by redefining this macro.
 */
#ifndef _9P_MAXFIDS
  #define _9P_MAXFIDS 16
#endif

/**
 * From intro(5):
 *   An exception is the tag NOTAG, defined as (ushort)~0 in <fcall.h>:
 *   the client can use it, when establishing a connection, to override
 *   tag matching in version messages.
 */
#define _9P_NOTAG (uint16_t)~0

/**
 * From attach(5):
 *   If the client does not wish to authenticate the connection, or
 *   knows that authentication is not required, the afid field in the
 *   attach message should be set to NOFID, defined as (u32int)~0 in
 *   <fcall.h>.
 */
#define _9P_NOFID (uint32_t)~0

/**
 * Enum defining sizes of 8..64 bit fields in bytes.
 */
enum {
	BIT8SZ  = 1, /**< Size of 8 bit field in bytes. */
	BIT16SZ = 2, /**< Size of 16 bit field in bytes. */
	BIT32SZ = 4, /**< Size of 32 bit field in bytes. */
	BIT64SZ = 8, /**< Size of 64 bit field in bytes. */
};

/**
 * Enum defining various global variables.
 */
enum {
	/**
	 * Size of the 9P packet header. Actually intro(5) never mention a
	 * header but the first 7 byte of every valid 9P message are always the
	 * same so I am going to take the liberty of refering to those 7 bytes
	 * as `header` in the comments.
	 */
	_9P_HEADSIZ = BIT32SZ + 1 + BIT16SZ,

	/**
	 * Size of a qid consisting of one one-byte field, one four-byte
	 * field and one eight-byte field.
	 */
	_9P_QIDSIZ = BIT8SZ + BIT32SZ + BIT64SZ,

	/**
	 * Ample room for Twrite/Rread header (iounit). This has been
	 * copied from the Plan 9 sourcetree, the file can be found at
	 * the location `sys/include/fcall.h`.
	 */
	_9P_IOHDRSIZ = 24,

	/**
	 * Minimum size of the machine-independent directory entry,
	 * stat. This includes the leading 16-bit count. See stat(5) for
	 * more information.
	 */
	_9P_MINSTSIZ = 3 * BIT16SZ + BIT32SZ + _9P_QIDSIZ
		+ 3 * BIT32SZ + BIT64SZ + 4 * BIT16SZ,

	/**
	 * Maximum length of a version string in an R-message. The
	 * longest valid version string is the 7 characters `unknown`
	 * and additional byte is needed to store the terminating
	 * nullbyte.
	 */
	_9P_VERLEN = 8,

	/**
	 * Used in the ::_9pattach function to identify the root
	 * directory of the desired file tree.
	 */
	_9P_ROOTFID = 1,

	/**
	 * From walk(5):
	 *   To simplify the implementation of the servers, a maximum of
	 *   sixteen name elements or qids may be packed in a single message.
	 */
	_9P_MAXWEL = 16,

	/**
	 * Path seperator used to split a path into nwnames.
	 */
	_9P_PATHSEP = '/',

	/**
	 * The library assumes that all T-Messages expect Tread and
	 * Twrite can be transmitted in a single PDU. The minmum msize
	 * required to ensure that this is the case is defined by this
	 * value.
	 */
	_9P_MINSIZE = 64,
};

/**
 * From stat(5):
 *   The mode contains permission bits as described in intro(5)
 *   and the following: 0x80000000 (DMDIR, this file is a direc-
 *   tory), 0x40000000 (DMAPPEND, append only), 0x20000000
 *   (DMEXCL, exclusive use), 0x04000000 (DMTMP, temporary);
 *   these are echoed in Qid.type.
 *
 * These definition have been copied from the Plan 9 sourcetree.
 * The file can be found at the location `sys/include/libc.h`.
 */
#define DMDIR    0x80000000 /**< Mode bit for directories. */
#define DMAPPEND 0x40000000 /**< Mode bit for append only files. */
#define DMEXCL   0x20000000 /**< Mode bit for exclusive use files. */
#define DMMOUNT  0x10000000 /**< Mode bit for mounted channel. */
#define DMAUTH   0x08000000 /**< Mode bit for authentication file. */
#define DMTMP    0x04000000 /**< Mode bit for non-backed-up files. */
#define DMREAD   0x4        /**< Mode bit for read permission. */
#define DMWRITE  0x2        /**< Mode bit for write permission. */
#define DMEXEC   0x1        /**< Mode bit for execute permission. */

/**
 * Same bits as defined above for Qid.type.
 */
#define QTDIR    0x80 /* Type bit for directories. */
#define QTAPPEND 0x40 /* Type bit for append only files. */
#define QTEXCL   0x20 /* Type bit for exclusive use files. */
#define QTMOUNT  0x10 /* Type bit for mounted channel. */
#define QTAUTH   0x08 /* Type bit for authentication file. */
#define QTTMP    0x04 /* Type bit for not-backed-up file. */
#define QTFILE   0x00 /* Plain file. */

/* From open(5):
 *   The mode field determines the type of I/O: 0 (called OREAD in
 *   <libc.h>), 1 (OWRITE), 2 (ORDWR), and 3 (OEXEC) mean read access,
 *   write access, read and write access, and execute access, to be
 *   checked against the per- missions for the file. In addition, if
 *   mode has the OTRUNC (0x10) bit set, the file is to be truncated
 *   [...]
 *
 * These definition have been copied from the Plan 9 sourcetree.
 * The file can be found at the location `sys/include/libc.h`.
 */
#define OREAD  0  /**< open for read */
#define OWRITE 1  /**< write */
#define ORDWR  2  /**< read and write */
#define OTRUNC 16 /**< or'ed in (except for exec), truncate file first */

/**
 * Valid values for the type field of a 9P message. This has been copied
 * from the Plan 9 sourcetree, the file can be found at the location
 * `sys/include/fcall.h`.
 */
typedef enum {
	Tversion =      100,
	Rversion,
	Tauth =         102,
	Rauth,
	Tattach =       104,
	Rattach,
	Terror =        106,    /* illegal */
	Rerror,
	Tflush =        108,
	Rflush,
	Twalk =         110,
	Rwalk,
	Topen =         112,
	Ropen,
	Tcreate =       114,
	Rcreate,
	Tread =         116,
	Rread,
	Twrite =        118,
	Rwrite,
	Tclunk =        120,
	Rclunk,
	Tremove =       122,
	Rremove,
	Tstat =         124,
	Rstat,
	Twstat =        126,
	Rwstat,
	Tmax,
} _9ptype;

/**
 * Enum defining operating which should be performed on the fid table.
 */
typedef enum {
	ADD, /**< Add the given fid to the table. */
	GET, /**< Get the given fid from the table. */
	DEL, /**< Delete the given fid from the table. */
} _9pfidop;

/**
 * Struct representing a qid.
 *
 *  From intro(5):
 *   The thirteen-byte qid fields hold a one-byte type, specifying
 *   whether the file is a directory, append-only file, etc., and two
 *   unsigned integers: first the four-byte qid version, then the eight-
 *   byte qid path.
 */
typedef struct {
	/**
	 * From intro(5):
	 *   [...] Whether the file is a directory, append-only file,
	 *   etc. [...]
	 */
	uint8_t type;

	/**
	 * From intro(5):
	 *   The version is a version number for a file; typically, it
	 *   is incremented every time the file is modified.
	 */
	uint32_t vers;

	/**
	 * From intro(5):
	 *   The path is an integer unique among all files in the
	 *   hierarchy. If a file is deleted and recreated with the same
	 *   name in the same directory, the old and new path components
	 *   of the qids should be different.
	 */
	uint64_t path;
} _9pqid;

/**
 * Struct representing an open fid.
 *
 * From intro(5):
 *   Most T-messages contain a fid, a 32-bit unsigned integer that the
 *   client uses to identify a ``current file'' on the server. Fids are
 *   somewhat like file descriptors in a user process, [...] all files
 *   being manipulated by the operating system - are identified by fids.
 */
typedef struct {
	/**
	 * The 32-bit unsigned integer representation of this fid.
	 */
	uint32_t fid;

	/**
	 * The qid associated with this fid.
	 */
	_9pqid qid;

	/**
	 * Offset in file associated with this fid.
	 */
	uint64_t off;

	/**
	 * iounit for this file as returned by open(5).
	 */
	uint32_t iounit;
} _9pfid;

/**
 * Struct representing a 9P package. For the sake of simplicity it only
 * has length, tag and type fields since those are the only fields
 * present in all 9P messages. Message specific parameters have to be
 * parsed manually.
 */
typedef struct {
	/**
	 * Pointer to the beginning of the message specific parameters.
	 * Values in this buffer have to be encoded in little-endian
	 * byte order.
	 */
	uint8_t *buf;

	/**
	 * Length of the message specific parameters. The length of the
	 * total message as specified by intro(5) can be calculated by
	 * adding the length of the header (7 bytes) to the value of
	 * this field.
	 */
	uint32_t len;

	/**
	 * Type of the message.
	 */
	_9ptype type; /* XXX Use uint8_t instead? */

	/**
	 * Unique message tag of this particular message.
	 */
	uint16_t tag;
} _9ppkt;

/**
 * Function used for receiving data that should be parsed as a 9P
 * R-messages and for sending T-messages to the server.
 *
 * @param buf Pointer where the data should be written to / read from.
 * @param bufsiz Maximum space available at @p data.
 * @return The number of bytes read on success.
 * @return `0`, if no read data is available, but everything is in order.
 * @return A negative errno value on error.
 */
typedef ssize_t (*iofunc)(void *buf, size_t bufsiz);

/**
 * Connection context for a 9P connection.
 */
typedef struct {
	/**
	 * Global static buffer used for storing message specific
	 * parameters.  Message indepented parameters are stored on the
	 * stack using `_9ppkt`.
	 */
	uint8_t buffer[_9P_MSIZE];

	/**
	 * Function used to receive R-messages.
	 */
	iofunc read;

	/**
	 * Function used to send T-messages.
	 */
	iofunc write;

	/**
	 * From version(5):
	 *   The client suggests a maximum message size, msize, that is
	 *   the maximum length, in bytes, it will ever generate or
	 *   expect to receive in a single 9P message.
	 *
	 * The msize suggested to the server is defined by the macro
	 * ::_9P_MSIZE. Since the server can choose an msize smaller
	 * than the one suggested, the actual msize used for the
	 * communication is stored in this variable.
	 *
	 * It is initialized with the value of ::_9P_MSIZE to make it
	 * possible to use this variable as an argument to the read
	 * function even before a session is established.
	 */
	uint32_t msize;

	/**
	 * This buffer is used for storing open fids. It should only be
	 * accessed and manipulated using the ::fidtbl function.
	 */
	_9pfid fids[_9P_MAXFIDS];
} _9pctx;

void _9pinit(_9pctx*, iofunc, iofunc);
int _9pversion(_9pctx*);
int _9pattach(_9pctx*, _9pfid**, char*, char*);
int _9pclunk(_9pctx*, _9pfid*);
int _9pstat(_9pctx*, _9pfid*, struct stat*);
int _9pwalk(_9pctx*, _9pfid**, char*);
int _9popen(_9pctx*, _9pfid*, uint8_t);
int _9pcreate(_9pctx*, _9pfid*, char*, uint32_t, uint8_t);
ssize_t _9pread(_9pctx*, _9pfid*, char*, size_t);
ssize_t _9pwrite(_9pctx*, _9pfid*, char*, size_t);
int _9premove(_9pctx*, _9pfid*);

#endif
