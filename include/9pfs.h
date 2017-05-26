#ifndef NINEP_H
#define NINEP_H

#include <stdint.h>
#include <stddef.h>
#include <sys/stat.h>

#include "net/sock/tcp.h"

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
  #define _9P_MSIZE 8192
#endif

/**
 * Timeout used for reading from the connection socket. Defaults to not
 * using a timeout at all potentially waiting for ever if no data is
 * received.
 */
#ifndef _9P_TIMOUT
  #define _9P_TIMOUT SOCK_NO_TIMEOUT
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
 * We need to map fids to qids, we do this using a primitve hash table
 * implementation. The amount of maximum entries in this hash table can
 * be tweaked by defining this macro.
 */
#ifndef _9P_MAXFIDS
  #define _9P_MAXFIDS 256
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
	 * Size of the machine-independent directory entry, stat. This
	 * includes the leading 16-bit count. See stat(5) for more
	 * information.
	 */
	_9P_STATSIZ = BIT16SZ + _9P_QIDSIZ + 5 * BIT16SZ + 4 * BIT32SZ + BIT64SZ,

	/**
	 * Maximum length of a version string in a R-message. The
	 * longest valid version string is the 7 characters `unknown`.
	 * In addition to that we need to reserve one-byte for the
	 * nullbyte.
	 */
	_9P_VERLEN = 8,

	/**
	 * Used in the ::_9pattach function to identify the root
	 * directory of the desired file tree.
	 */
	_9P_ROOTFID = 1,

	/**
	 * Maximum size of a file path as used in a fid.
	 */
	_9P_PTHMAX = 32,

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
};

/**
 * From stat(5):
 *   The mode contains permission bits as described in intro(5)
 *   and the following: 0x80000000 (DMDIR, this file is a direc-
 *   tory), 0x40000000 (DMAPPEND, append only), 0x20000000
 *   (DMEXCL, exclusive use), 0x04000000 (DMTMP, temporary);
 *   these are echoed in Qid.type.
 */
#define DMDIR    0x80000000 /**< Mode bit for directories. */
#define DMAPPEND 0x40000000 /**< Mode bit for append only files. */
#define DMEXCL   0x20000000 /**< Mode bit for exclusive use files. */
#define DMTMP    0x04000000 /**< Mode bit for non-backed-up file. */

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
	 * Path of this fid on the server.
	 */
	char path[_9P_PTHMAX];

	/**
	 * The 32-bit unsigned integer representation of this fid.
	 */
	uint32_t fid;

	/**
	 * The qid associated with this fid.
	 */
	_9pqid qid;
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
 * Macro to advandce the position in the packet buffer. The macro takes
 * care of decrementing the length field of the packet as well.
 *
 * @param PKT Pointer to a packet in which the buffer position should be
 *   advanced.
 * @param OFF Offset which should be added to the buffer position.
 */
#define ADVBUF(PKT, OFF) \
	do { (PKT)->buf += OFF; (PKT)->len -= OFF; } while (0)

int _9pinit(sock_tcp_ep_t);
void _9pclose(void);

int _9pversion(void);
int _9pattach(_9pfid**, char*, char*);
int _9pstat(_9pfid*, struct stat*);
_9pfid* _9pwalk(char*);

_9pfid* _fidtbl(uint32_t, _9pfidop);
_9pfid* newfid(void);

uint8_t* _pstring(uint8_t*, char*);
int _hstring(char*, uint16_t, _9ppkt*);
int _hqid(_9pqid*, _9ppkt*);

uint8_t* _htop8(uint8_t*, uint8_t);
uint8_t* _htop16(uint8_t*, uint16_t);
uint8_t* _htop32(uint8_t*, uint32_t);
uint8_t* _htop64(uint8_t*, uint64_t);

void _ptoh8(uint8_t *dest, _9ppkt*);
void _ptoh16(uint16_t *dest, _9ppkt*);
void _ptoh32(uint32_t *dest, _9ppkt*);
void _ptoh64(uint64_t *dest, _9ppkt*);

#endif
