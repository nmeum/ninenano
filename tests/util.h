/**
 * Retrieve value of environment variable using getenv(3) and return
 * `EXIT_FAILURE` if the result was a NULL pointer.
 *
 * @param VAR Name of the variable to store result in.
 * @param ENV Name of the environment variable.
 */
#define GETENV(VAR, ENV) \
	do { if (!(VAR = getenv(ENV))) { \
		fprintf(stderr, "%s is not set or empty\n", ENV); \
		return EXIT_FAILURE; } \
	} while (0)

/**
 * Global socket for 9P protocol connection.
 */
static sock_tcp_t psock;

/**
 * Function used to receive data from a TCP sock connection. This
 * function is intended to be used as an ::iofunc for ::_9pinit.
 */
static ssize_t
recvfn(void *buf, size_t count)
{
	return sock_tcp_read(&psock, buf, count, SOCK_NO_TIMEOUT);
}

/**
 * Function used to send data to a TCP server. This function is intended
 * to be used as an ::iofunc for ::_9pinit.
 */
static ssize_t
sendfn(void *buf, size_t count)
{
	return sock_tcp_write(&psock, buf, count);
}
