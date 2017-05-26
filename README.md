9RIOT
=====

Implementation of the 9P protocol for the RIOT operating system.

Documentation
=============

TODO

Tests
=====

TODO

Usage
=====

TODO

Roadmap
=======

* [ ] Simplify `_fibtbl` function
* [ ] Better error codes for _9pattach and _9pwalk
* [ ] Buffer overflow checks for insertion commands
* [ ] More DEBUG calls
* [ ] s/char/unsiged char/
* [ ] Only do certian checks when compiled with -DDDEVHELP + assert(3)
* [ ] Better errno return values to differentiate paths in unit tests
* [ ] Consider comparison with end pointer to detect buffer overflow in packets
* [ ] Refactor Documentation
* [ ] Implement missing message types

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
