#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

void
initrand(void)
{
	uint8_t seed;
	int fd;

	/* If we can't read from /dev/random we will use a hardcode
	 * value for seeding which not optimal but sufficient. */

	if ((fd = open("/dev/random", O_RDONLY)) == -1)
		seed = 23;
	if (read(fd, &seed, 1) != 1)
		seed = 42;

	close(fd);
	srand(seed);
}

uint32_t
randu32(void)
{
	return rand();
}
