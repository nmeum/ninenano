#include <stdint.h>

uint16_t
byteorder_swaps(uint16_t v)
{
	return __builtin_bswap16(v);
}

uint32_t
byteorder_swapl(uint32_t v)
{
	return __builtin_bswap32(v);
}

uint64_t
byteorder_swapll(uint64_t v)
{
	return __builtin_bswap64(v);
}
