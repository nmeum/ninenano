#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "9util.h"

void
initrand(void)
{
	uint8_t seed;
	int fd;

	/* If we can't read from /dev/random we will use a hardcode
	 * value for seeding which not optimal but sufficient. */

	if ((fd = open("/dev/random", O_RDONLY)) == -1) {
		seed = 23;
		goto srand;
	}

	if (read(fd, &seed, 1) != 1)
		seed = 42;
	close(fd);

srand:
	srand(seed);
}

uint32_t
randu32(void)
{
	int ret;

	/* From rand(3):
	 *   The rand() function returns a result in the range of 0 to
	 *   `RAND_MAX`.
	 */
	ret = rand();
	assert(ret >= 0);

	return (uint32_t)rand();
}
