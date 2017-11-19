#include "xtimer.h"
#include "random.h"
#include "9util.h"

void
initrand(void)
{
	random_init(xtimer_now().ticks32);
}

uint32_t
randu32(void)
{
	return random_uint32();
}
