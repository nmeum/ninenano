#include <string.h>

#include "9pfs.h"

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
 * \defgroup Functions for converting from host byte order to the byte
 *   order used by the 9P protocol.
 *
 * Functions for converting integers encoded using the byte order used
 * by the current CPU (the host system) to the one used by the protocol
 * (little-endian).
 *
 * To receive the integer which should be converted those functions read
 * a certain amount of bytes from the given buffer. Afterwards the
 * position in the buffer is advanced by the amount of bytes read and a
 * pointer to the new position is returned.
 *
 * @{
 */

uint8_t*
_htop8(uint8_t *buf, uint8_t val)
{
	*buf = val;
	buf += BIT8SZ;
	return buf;
}

uint8_t*
_htop16(uint8_t *buf, uint16_t val)
{
	val = _9p_swap(val, s);
	memcpy(buf, &val, BIT16SZ);

	buf += BIT16SZ;
	return buf;
}

uint8_t*
_htop32(uint8_t *buf, uint32_t val)
{
	val = _9p_swap(val, l);
	memcpy(buf, &val, BIT32SZ);

	buf += BIT32SZ;
	return buf;
}

uint8_t*
_htop64(uint8_t *buf, uint64_t val)
{
	val = _9p_swap(val, l);
	memcpy(buf, &val, BIT64SZ);

	buf += BIT64SZ;
	return buf;
}

/**@}*/

/**
 * \defgroup Functions for converting from the byte order used by the 9P
 *   protocol to the byte order used by the host.
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
_ptoh8(uint8_t *dest, _9ppkt *pkt)
{
	*dest = *pkt->buf;
	pkt->buf += BIT8SZ;
	pkt->len -= BIT8SZ;
}

void
_ptoh16(uint16_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT16SZ);
	*dest = _9p_swap(*dest, s);

	pkt->buf += BIT16SZ;
	pkt->len -= BIT16SZ;
}

void
_ptoh32(uint32_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT32SZ);
	*dest = _9p_swap(*dest, l);

	pkt->buf += BIT32SZ;
	pkt->len -= BIT32SZ;
}

void
_ptoh64(uint64_t *dest, _9ppkt *pkt)
{
	memcpy(dest, pkt->buf, BIT64SZ);
	*dest = _9p_swap(*dest, ll);

	pkt->buf += BIT64SZ;
	pkt->len -= BIT64SZ;
}

/**@}*/

/**
 * \defgroup Functions for converting strings from host representation
 * to protocol representation and vice versa.
 * @{
 */

/**
 * From intro(5):
 *   The notation string[s] (using a literal s character) is shorthand
 *   for s[2] followed by s bytes of UTF-8 text.
 *
 * Converts the null-terminated string in the given buffer to a string
 * as defined in intro(5) prefixed with a two byte size field.
 *
 * Besides the position of the given buf pointer is advanced and a
 * pointer to the new position is returned.
 *
 * @param buf Pointer to a buffer to which the resulting string should
 *   be written.
 * @param str Pointer to the null-terminated string.
 * @return Pointer to memory location in `buf` right behind the newly
 *   written string.
 */
uint8_t*
_pstring(uint8_t *buf, char *str)
{
	uint16_t len, siz;

	if (!str) {
		siz = _9p_swap(0, s);
		memcpy(buf, &siz, BIT16SZ);
		buf += BIT16SZ;
		return 0;
	}

	len = strlen(str);
	buf = _htop16(buf, len);

	memcpy(buf, str, len);
	buf += len;

	return buf;
}

/**
 * This function converts a string as represented by the host to a
 * string as represented by the protocol.
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
_hstring(char *dest, uint16_t n, _9ppkt *pkt)
{
	uint16_t siz;

	if (pkt->len <= BIT16SZ)
		return -1;
	_ptoh16(&siz, pkt);

	if (pkt->len < siz || siz >= n)
		return -1;

	memcpy(dest, pkt->buf, siz);
	dest[siz] = '\0';

	pkt->buf += siz;
	pkt->len -= siz;

	return 0;
}

/**@}*/
