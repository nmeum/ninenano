RIOTBASE ?= "$(CURDIR)"/vendor/RIOT

ifeq ($(TESTADDR),)
  $(error 'TESTADDR' environment variable isn't set)
endif

test: all
	cd tests/ && ./run_tests.sh "$(TESTADDR)"

all:
	"$(MAKE)" -C tests/unit/client
	"$(MAKE)" -C tests/unit/server
	"$(MAKE)" -C tests/integration/client
	"$(MAKE)" -C tests/integration/server

.PHONY: all test
