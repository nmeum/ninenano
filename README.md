**GEFAHR ACHTUNG GEFAHR:** THIS CODE IS WRITTEN IN A MEMORY UNSAFE
PROGRAMMING LANGUAGE AND ACCEPTS ARBITRARY INPUT FROM A NETWORK
CONNECTION. I AM PRETTY SURE THAT I AM NOT CAPABLE OF WRITING BUG-FREE C
CODE SO PLEASE DON'T BLAME ME IF YOUR CONSTRAINED DEVICE BECOMES PART OF
THE NEXT INTERNET OF THINGS BOTNET. KTHXBYE!

ninenano
========

ninenano is a client implementation of the 9P network for [constrained
devices][7]. It currently consists of two separated components:

1. 9p: A client implementation of the 9P network protocol
2. 9pfs: Virtual 9P file system using RIOTs VFS layer

The 9p component works on the [RIOT][3] operating system and on various
POSIX compatible operating systems. It uses only a small subset of the
functions provided by the libc and should therefore also be portable to
other operating systems and microcontroller frameworks. The 9pfs
component uses RIOT's [VFS layer][5] and thus only works on RIOT.

However, even on RIOT you might consider using the 9p component directly
instead of 9pfs component since doing so will spare you some bytes in
the text segment.

Status
------

This implementation of the 9P protocol has the following limitations:

1. It only sends one T-message at a time and waits for an R-message from
   the server before sending additional T-messages.
2. `flush(5)` is not implemented because it wasn't needed due to the
   first limitation.
3. Only files with a maximum amount of sixteen path elements can be
   accessed using `_9pwalk`.
4. Proper handling of `Rerror` messages is not implemented.
5. `Twstat` is currently not implemented.

*The fourth and fifth limitation might be resolved in feature releases.*

Supported compilers
-------------------

In theory this code should compile with any C99 compiler, however, the
compatibility layer for POSIX compatible operating systems uses gcc
builtins for byteswapping. Therefore the code only compiles with recent
version of gcc and clang.

Compilation
-----------

For using this library on RIOT you should use the provided RIOT ninenano
pkg. For compiling a static library for POSIX operating systems run the
following command:

	$ make -C mk/POSIX

A few CPP macros are provided for tweaking the size of the library,
these can be passed by modifying the `CFLAGS` environment variable
accordingly. Currently the following macros can be redefined:

1. `_9P_MSIZE`: Maximum message size and therefore also equal to the
   buffer size used to store 9P messages.
2. `_9P_MAXFIDS`: Maximum amount of open fids for a 9P connection.

In addition to that you can define `NDEBUG` to prevent some optional
sanity checks form being included in the resulting binary. Your
application should be fine without them they only detect violations of
invariants caused by programming errors.

Usage
-----

**Disclaimer:** This library requires a basic understanding of the 9P
network protocol. If you don't know how 9P works start by reading the
`intro(5)` [man page][6] shipped with the fourth edition of Plan 9
operating system.

The API is very straight forward and shouldn't change a lot anymore. If
you want to use 9P in combination with the provided VFS layer start by
reading the [VFS documentation][5]. In order to use 9pfs you need to
point the `private_data` member of your `vfs_mount_t` to an allocated
`_9pfs` superblock in addition to that the `ctx` member of the
superblock needs to be initialized using `_9pinit`.

If you want to use the 9P component directly all you have to do is
allocate memory for a `_9pctx` and afterwards initialize it with
`_9pinit`. Consult the documentation or the examples in `examples/` and
`tests/` if you need additional help.

Documentation
-------------

The source code is documented in a format that can be parsed by
[Doxygen][4]. A `Doxyfile` is also provided. To generate source code
documentation using the supplied `Doxyfile` run the following command:

	$ doxygen Doxyfile

Afterwards the documentation will be located in a newly created folder
called `docs`, view the docs by opening `docs/index.html` in your
favorite web browser.

Tests
-----

ninenano comes with both unit and integration tests utilising RIOT's
test framework. The unit tests use the 9p component directly while the
integration tests use the 9p component indirectly through the 9pfs
component.

In order to run the tests you need to setup the toolchain for RIOT's
[native family][1]. Besides you need to install go `>= 1.5`, python
`3.X` and the [pexpect][2] python package.

In addition to that a tun devices needs to created:

	# ip tuntap add tap0 mode tap
	# ip addr add fe80::e42a:1aff:feca:10ec dev tap0
	# ip link set tap0 up

After creating the tun devices you can run the tests:

	$ export TESTADDR="fe80::e42a:1aff:feca:10ec"
	$ make test

**Note:** RIOT will add an additional IP address to the interface when
you run the tests for the first time. However, it doesn't matter which
one you use as a value for the `TESTADDR` environment variable, meaning
you can ignore the additional IP address.

License
-------

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <http://www.gnu.org/licenses/>.

[1]: https://github.com/RIOT-OS/RIOT/wiki/Family:-native#toolchains
[2]: https://pypi.python.org/pypi/pexpect
[3]: http://riot-os.org/
[4]: http://www.stack.nl/~dimitri/doxygen/
[5]: http://riot-os.org/api/group__sys__vfs.html
[6]: http://man.cat-v.org/plan_9/5/intro
[7]: https://tools.ietf.org/html/rfc7228
