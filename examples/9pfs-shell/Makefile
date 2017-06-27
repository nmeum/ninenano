# Name of your application
APPLICATION = 9pfs-shell

# Board for which the application should be compiled
BOARD ?= native

# Disable optional assert(3) checks
CFLAGS += -DNDEBUG

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

include $(RIOTBASE)/Makefile.include
