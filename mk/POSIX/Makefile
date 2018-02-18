OBJS  := 9p.o util.o byteorder.o random.o
VPATH += ../../9p:../../compat/POSIX

CFLAGS ?= -O0 -g
CFLAGS += -I../../include -I../../compat/include/POSIX
CFLAGS += -std=c99 -D_XOPEN_SOURCE -Werror \
	  -Wpedantic -Wall -Wextra -Wconversion

ifeq "$(findstring clang,$(shell $(CC) --version))" "clang"
	CFLAGS += -Weverything -Wno-padded -Wno-disabled-macro-expansion
endif

%.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS)

ninenano.a: $(OBJS)
	$(AR) rcs $@ $?
