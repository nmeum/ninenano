#ifndef NINEBYTE_H
#define NINEBYTE_H

#include <stdint.h>

uint16_t byteorder_swaps(uint16_t);
uint32_t byteorder_swapl(uint32_t);
uint64_t byteorder_swapll(uint64_t);

#endif
