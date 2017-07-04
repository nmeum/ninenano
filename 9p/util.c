#include <assert.h>
#include <string.h>

#include <sys/types.h>

#include "9p.h"
#include "random.h"
#include "byteorder.h"

/**
 * From intro(5):
 *   Each message consists of a sequence of bytes. Two-, four-, and
 *   eight-byte fields hold unsigned integers represented in
 *   little-endian order (least significant byte first).
 *
 * Since we want to write an endian-agnostic implementation we define a
 * macro called `_9p_swap` which is similar to `_byteorder_swap` but
 * doesn't swap the byte order on little endian plattforms.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define _9p_swap(V, T) (V)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define _9p_swap(V, T) (byteorder_swap##T((V)))
#else
#   error "Byte order is neither little nor big!"
#endif

/**
 * @defgroup pktBuf Functions for packet buffers.
 *
 * @{
 */

/**
 * Advances the position in the packet buffer. The macro takes care of
 * decrementing the length field of the packet as well.
 *
 * @param pkt Pointer to a packet in which the buffer position should be
 * 	advanced.
 * @param off Offset which should be added to the buffer position.
 */
void
advbuf(_9ppkt *pkt, size_t off)
{
	assert(pkt->len - off <= pkt->len);

	pkt->buf += off;
	pkt->len -= off;
}

/**
 * Copies n bytes from the given memory area to a packet buffer and
 * afterwards advances the position in the packet buffer.
 *
 * @param pkt Pointer to a packet to which the data should be copied.
 * @param src Pointer to a buffer from which the data should be read.
 * @param n Amount of bytse which should be copied.
 */
void
bufcpy(_9ppkt *pkt, void *src, size_t n)
{
	memcpy(pkt->buf, src, n);
	advbuf(pkt, n);
}

/**@}*/

/**
 * @defgroup fidTbl Functions for the fid table.
 *
 * @{
 */

/**
 * This function can be used to add, get and delete fids. It has one
 * unexpected caveat: The add operation does set the `fid` field, it
 * simply returns the next free fid. Thus the callers has to set the
 * `fid` field on the struct pointer manually.
 *
 * @pre fid != 0
 *
 * @param fids Pointer to fid table which should be modified. The table
 *   size must be equal to ::_9P_MAXFIDS.
 * @param fid A 32-bit unsigned integer that the client uses to identify
 *   a `current file` on the server.
 * @param op Operating which should be performed for the given fid on
 *   the fid table.
 * @return On success a pointer to a fid in the fid table is returned,
 *   on failure a NULL pointer is returned instead.
 */
_9pfid*
fidtbl(_9pfid *fids, uint32_t fid, _9pfidop op)
{
	_9pfid *ret;
	size_t i, hash;

	/* A value of 0 is used to indicate an unused table entry. */
	if (!fid)
		return NULL;

	hash = i = fid % _9P_MAXFIDS;
	if (op == ADD)
		fid = 0;

	do {
		if ((ret = &fids[i])->fid == fid)
			break;

		i = (i + 1) % _9P_MAXFIDS;
	} while (i != hash);

	if (ret->fid != fid)
		return NULL;
	if (op == DEL) {
		if (ret->fid == _9P_ROOTFID)
			return NULL;
		ret->fid = 0;
	}

	return ret;
}

/**
 * Finds a new **unique** fid for the fid table. Insert it into the fid
 * table and returns a pointer to the new fid table entry.
 *
 * @param fids Pointer to fid table which should be modified. The table
 *   size must be equal to ::_9P_MAXFIDS.
 * @return Pointer to fid table entry or NULL if the fid table is full.
 */
_9pfid*
newfid(_9pfid *fids)
{
	_9pfid *r;
	size_t i;
	uint32_t fid;

	for (i = 1; i < _9P_MAXFIDS; i++) {
		fid = random_uint32_range(1, UINT32_MAX);
		if (fidtbl(fids, fid, GET))
			continue;

		/* TODO room for optimization don't call fidtbl twice. */
		r = fidtbl(fids, fid, ADD);
		assert(r != NULL);

		r->fid = fid;
		return r;
	}

	return NULL;
}

/**@}*/

/**
 * @defgroup htop Functions for converting from host byte order to the \
 *   byte order used by the 9P protocol.
 *
 * Functions for converting integers encoded using the byte order used
 * by the current CPU (the host system) to the one used by the protocol
 * (little-endian).
 *
 * These functions take two arguments: An unsigned integer and a pointer
 * to a packet buffer to which the resulting integer in the protocol
 * representation should be written. The position in the packet buffer
 * is advanced after writting the integer to it.
 *
 * @{
 */

void
htop8(uint8_t val, _9ppkt *pkt)
{
	*pkt->buf = val;
	advbuf(pkt, BIT8SZ);
}

void
htop16(uint16_t val, _9ppkt *pkt)
{
	val = _9p_swap(val, s);
	bufcpy(pkt, &val, BIT16SZ);
}

void
htop32(uint32_t val, _9ppkt *pkt)
{
	val = _9p_swap(val, l);
	bufcpy(pkt, &val, BIT32SZ);
}

void
htop64(uint64_t val, _9ppkt *pkt)
{
	val = _9p_swap(val, ll);
	bufcpy(pkt, &val, BIT64SZ);
}

/**@}*/

/**
 * @defgroup ptoh Functions for converting from the byte order used by \
 *   the 9P protocol to the byte order used by the host.
 *
 * Functions for converting integers encoded using the byte order used
 * by the 9P protocol to the one used by the CPU (the host system).
 *
 * To receive the integer which should be converted those functions read
 * a certain amount of bytes from the buffer pointed to by the `buf`
 * field in the given `_9ppkt`. After reading the integer the position
 * in the buffer is advanced by the amount of bytes read. Besides the
 * length of the 9P packet is updated.
 *
 * @{
 */

void
ptoh8(uint8_t *dest, _9ppkt *pkt)
{
	*dest = *pkt->buf;
	advbuf(pkt, BIT8SZ);
}

void
ptoh16(uint16_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT16SZ);
	*dest = _9p_swap(*dest, s);
	advbuf(pkt, BIT16SZ);
}

void
ptoh32(uint32_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT32SZ);
	*dest = _9p_swap(*dest, l);
	advbuf(pkt, BIT32SZ);
}

void
ptoh64(uint64_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT64SZ);
	*dest = _9p_swap(*dest, ll);
	advbuf(pkt, BIT64SZ);
}

/**@}*/

/**
 * @defgroup strings Functions for converting strings from host \
 *   representation to protocol representation and vice versa.
 *
 * @{
 */

/**
 * Does the same thing as ::pstring except for the fact that this
 * function takes a string length parameter and therefore also works on
 * strings which are not null-terminated.
 *
 * @param str Pointer to a buffer containing a string.
 * @param len Length of the string contained in the buffer.
 * @param pkt Pointer to a packet to which the resulting string should
 * 	be written.
 * @return `0` on success.
 * @return `-1` on failure.
 */
int
pnstring(char *str, size_t len, _9ppkt *pkt)
{
	if (len + BIT16SZ > pkt->len)
		return -1;

	htop16((uint16_t)len, pkt);
	if (len) bufcpy(pkt, str, len);

	return 0;
}

/**
 * From intro(5):
 *   The notation string[s] (using a literal s character) is shorthand
 *   for s[2] followed by s bytes of UTF-8 text.
 *
 * Converts the null-terminated string in the given buffer to a string
 * as defined in intro(5) prefixed with a two byte size field.
 *
 * Besides the position of the given packet buffer is advanced. This
 * function might fail if the string length exceeds the amount of bytes
 * available in the packet buffer.
 *
 * @param str Pointer to the null-terminated string.
 * @param pkt Pointer to a packet to which the resulting string should
 * 	be written.
 * @return `0` on success.
 * @return `-1` on failure.
 */
int
pstring(char *str, _9ppkt *pkt)
{
	size_t len;

	len = (str) ? strlen(str) : 0;
	return pnstring(str, len, pkt);
}

/**
 * This function converts a string as represented by the protocol to a
 * string as represented by the host.
 *
 * From intro(5):
 *   The notation string[s] (using a literal s character) is shorthand
 *   for s[2] followed by s bytes of UTF-8 text.
 *
 * We thus read the first two bytes of the memory location pointed to by
 * the `buf` field of the given 9P packet. We use those two bytes to
 * determine the length of the string. If this length exceeds the packet
 * length or the given parameter `n` an error is returned. Otherwise we
 * copy the bytes after the length field to the given destination address.
 *
 * The parameter `n` is a `uint16_t` value because the maximum length of
 * a 9P string is `2^16` since the length field consists of 2 bytes.
 *
 * @param dest Pointer to memory location to store string in. The string
 *   will always be null-terminated unless an error has occured.
 * @param n Size of the given buffer.
 * @param pkt 9P packet to read string from.
 * @return `0` on success.
 * @return `-1` on failure.
 */
int
hstring(char *dest, uint16_t n, _9ppkt *pkt)
{
	uint16_t siz;

	if (pkt->len < BIT16SZ)
		return -1;
	ptoh16(&siz, pkt);

	if (pkt->len < siz || siz >= n)
		return -1;

	memcpy(dest, pkt->buf, siz);
	dest[siz] = '\0';

	advbuf(pkt, siz);
	return 0;
}

/**@}*/

/**
 * @defgroup qids Functions for converting qids.
 *
 * @{
 */

/**
 * From intro(5):
 *   The thirteen-byte qid fields hold a one-byte type, specifying
 *   whether the file is a directory, append-only file, etc., and two
 *   unsigned integers: first the four-byte qid version, then the eight-
 *   byte qid path.
 *
 * This function converts such a qid representation to a ::_9pqid
 * struct.
 *
 * @param dest Pointer to an allocated ::_9pqid struct.
 * @param pkt 9P packet to read qid from.
 * @return `0` on success.
 * @return `-1` on failure.
 */
int
hqid(_9pqid *dest, _9ppkt *pkt)
{
	if (pkt->len < _9P_QIDSIZ)
		return -1;

	ptoh8(&dest->type, pkt);
	ptoh32(&dest->vers, pkt);
	ptoh64(&dest->path, pkt);

	return 0;
}

/**@}*/
