9RIOT
=====

Implementation of the 9P protocol for the RIOT operating system.

Documentation
=============

TODO

Tests
=====

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

Usage
=====

TODO

Roadmap
=======

Refactor existing code:

* [ ] Better error handling (parse Rerror messages)
* [ ] Simplify `_fibtbl` function
* [x] Better error codes for _9pattach and _9pwalk
* [ ] Check string length never exceeds UINT16_MAX
* [ ] Buffer overflow checks for insertion commands
* [ ] More DEBUG calls
* [ ] s/char/unsiged char/
* [ ] Only do certian checks when compiled with -DDDEVHELP + assert(3)
* [ ] Better errno return values to differentiate paths in unit tests
* [ ] Consider comparison with end pointer to detect buffer overflow in packets
* [ ] Check that strings passed to _pstring do not exceed UINT16_MAX
* [ ] 9pfs VFS layer
* [ ] Refactor Documentation

Implement missing message types:

* [x] Tcreate
* [x] Twrite
* [ ] Twstat

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
