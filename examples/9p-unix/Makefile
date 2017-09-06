CC ?= cc
RM ?= rm -f

CFLAGS ?= -O0 -g
CFLAGS += -std=c99 -static -Wpedantic -Wall -Wextra -Werror
CFLAGS += -I../../include -I ../../compat/POSIX/include

hello-client: main.c ../../ninenano.a
	$(CC) $(CFLAGS) -o $@ $^

../../ninenano.a:
	"$(MAKE)" -C ../../ -f Makefile.posix

clean:
	$(RM) hello-client
