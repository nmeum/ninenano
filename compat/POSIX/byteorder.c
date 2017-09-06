#include <stdint.h>

#define __bswap_constant_16(x) \
	((uint16_t) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))

uint16_t
byteorder_swaps(uint16_t v)
{
	return __bswap_constant_16(v);
}

#define __bswap_constant_32(x) \
	((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
	(((x) & 0x0000ff00) << 8) | (((x) & 0x000000ff) << 24))

uint32_t
byteorder_swapl(uint32_t v)
{
	return __bswap_constant_32(v);
}

#define __bswap_constant_64(x) \
	((((x) & 0xff00000000000000ull) >> 56)  \
	| (((x) & 0x00ff000000000000ull) >> 40) \
	| (((x) & 0x0000ff0000000000ull) >> 24) \
	| (((x) & 0x000000ff00000000ull) >> 8)  \
	| (((x) & 0x00000000ff000000ull) << 8)  \
	| (((x) & 0x0000000000ff0000ull) << 24) \
	| (((x) & 0x000000000000ff00ull) << 40) \
	| (((x) & 0x00000000000000ffull) << 56))

uint64_t
byteorder_swapll(uint64_t v)
{
	return __bswap_constant_64(v);
}
