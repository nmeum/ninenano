9RIOT
=====

Implementation of the 9P protocol for the RIOT operating system.

Usage
=====

9RIOT currently consists of two components:

1. 9p: An implementation of the 9P network protocol
2. 9pfs: Virtual 9P file system using RIOTs VFS layer

When writing IoT application that utilize 9P as an application layer
protocol it is probably sufficient to use the 9P component directly
without the VFS layer.

Documentation
=============

TODO

Tests
=====

9RIOT comes with both unit and integration tests. The unit tests use the
9P component directly while the integration tests use 9P through the
9pfs component.

In order to run the tests you need to setup the toolchain for RIOTs
[native family](1). Besides you need to install go >= 1.5, python 3.X
and the [pexpect](2) python package.

Besides a tun devices needs to created:

	# ip tuntap add tap0 mode tap
	# ip addr add fe80::e42a:1aff:feca:10ec dev tap0
	# ip link set tap0 up

After creating the tun devices you can run the tests:

	$ export TESTADDR="fe80::e42a:1aff:feca:10ec"
	$ make test

Roadmap
=======

* [ ] Implement vfs_rename
* [ ] Allow more than one connection
* [ ] Abstraction to allow different transport layers
* [ ] Better error handling (parse Rerror messages)
* [ ] Simplify `_fibtbl` function
* [x] Better error codes for _9pattach and _9pwalk
* [x] Buffer overflow checks for insertion commands
* [ ] More DEBUG calls
* [x] Only do certain checks when compiled with -DDDEVHELP + assert(3)
* [x] Better errno return values to differentiate paths in unit tests
* [x] 9pfs VFS layer
* [ ] Refactor Documentation

License
=======

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public
License as published by the Free Software Foundation, either
version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with this program. If not, see
<http://www.gnu.org/licenses/>.

[1]: https://github.com/RIOT-OS/RIOT/wiki/Family:-native#toolchains
[2]: https://pypi.python.org/pypi/pexpect
