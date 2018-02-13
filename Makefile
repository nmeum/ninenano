RIOTBASE ?= "$(CURDIR)"/vendor/RIOT

ifeq ($(TESTADDR),)
  $(error 'TESTADDR' environment variable isn't set)
endif

test: testsuite
	cd tests/ && ./run_tests.sh "$(TESTADDR)"

testsuite:
	"$(MAKE)" -C tests/unit/client
	"$(MAKE)" -C tests/unit/server
	"$(MAKE)" -C tests/integration/client
	"$(MAKE)" -C tests/integration/server

.PHONY: test testsuite
