CFLAGS += -D_POSIX_C_SOURCE=200112L

hello-client: main.o ninenano.a
	$(CC) $(CFLAGS) -o $@ $^

include ../../mk/POSIX/Makefile
