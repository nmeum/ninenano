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

# Modules required for using gnrc_tcp
USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_tcp

# Modules required for using 9pfs
USEMODULE += 9pfs

# Optional modules for debugging purposes
USEMODULE += gnrc_icmpv6_echo

export NINEBASE := $(CURDIR)/../..
include $(NINEBASE)/Makefile.riot
