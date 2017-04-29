#ifndef NINEP_H
#define NINEP_H

#include <stdint.h>
#include <stddef.h>

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
 *   The client suggests a maximum message size, msize, that is
 *   the maximum length, in bytes, it will ever generate or
 *   expect to receive in a single 9P message.
 */
#ifndef _9P_MSIZE
  #define _9P_MSIZE 8192
#endif

/**
 * Enum defining various global variables.
 */
enum {
	/**
	 * From intro(5):
	 *   An exception is the tag NOTAG, defined as (ushort)~0
	 *   in <fcall.h>: the client can use it, when establishing a
	 *   connection, to override tag matching in version messages.
	 */
	_9P_NOTAG = ~0,

	/**
	 * Size of the 9P packet header. Actually intro(5) never mention a
	 * header but the first 7 byte of every valid 9P message are always the
	 * same so I am going to take the liberty of refering to those 7 bytes
	 * as `header` in the comments.
	 */
	_9P_HEADSIZ = 7,

	/**
	 * Maximum length of a version string in a R-message. The
	 * longest valid version string is the 7 characters `unknown`.
	 * In addition to that we need to reserve one byte for the
	 * nullbyte.
	 */
	_9P_VERLEN = 8,
};

/**
 * Valid values for the type field of a 9P message. This has been copied
 * from the Plan 9 source, the file can be found at the location
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

int _9pinit(sock_tcp_ep_t);
void _9pclose(void);

int _9pversion(void);

/**
 * Enum defining sizes of 8..64 bit fields in bytes.
 */
enum {
	BIT8SZ  = 1, /**< Size of 8 bit field in bytes. */
	BIT16SZ = 2, /**< Size of 16 bit field in bytes. */
	BIT32SZ = 4, /**< Size of 32 bit field in bytes. */
	BIT64SZ = 8, /**< Size of 64 bit field in bytes. */
};

uint8_t* _pstring(uint8_t*, char*);
int _hstring(char*, uint16_t, _9ppkt*);

uint8_t* _htop8(uint8_t*, uint8_t);
uint8_t* _htop16(uint8_t*, uint16_t);
uint8_t* _htop32(uint8_t*, uint32_t);
uint8_t* _htop64(uint8_t*, uint64_t);

void _ptoh8(uint8_t *dest, _9ppkt*);
void _ptoh16(uint16_t *dest, _9ppkt*);
void _ptoh32(uint32_t *dest, _9ppkt*);
void _ptoh64(uint64_t *dest, _9ppkt*);

#endif
