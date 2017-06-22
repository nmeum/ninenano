# Name of your application
APPLICATION = 9pfs-shell

# Run tests of native only
BOARD = native

# Change this to 0 show compiler invocation lines
QUIET ?= 1

# Path to the RIOT submodule
RIOTBASE ?= $(CURDIR)/../../vendor/RIOT

# Modules required for using the vfs shell commands
USEMODULE += vfs
USEMODULE += shell
USEMODULE += shell_commands

# Modules required for using sock_tcp
USEMODULE += ipv6_addr
USEMODULE += lwip_ipv6_autoconfig
USEMODULE += inet_csum
USEMODULE += lwip_ethernet lwip_netdev
USEMODULE += lwip_sock_tcp
USEMODULE += netdev_default
USEMODULE += ps

# Module required for unit test functions
USEMODULE += embunit

include $(RIOTBASE)/Makefile.include
