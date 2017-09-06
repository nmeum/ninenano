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

	/* If we can't read from /dev/random random(3) will use the
	 * value 1 for seeding which not optimal but sufficient. */

	if ((fd = open("/dev/random", O_RDONLY)) == -1)
		return;
	if (read(fd, &seed, 1) != 1) {
		close(fd);
		return;
	}

	close(fd);
	srandom(seed);
}

uint32_t
randu32(void)
{
	return random();
}
