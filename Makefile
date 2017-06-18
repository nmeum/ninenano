RIOTBASE ?= "$(CURDIR)"/vendor/RIOT

ifeq ($(TESTADDR),)
  $(error 'TESTADDR' environment variable isn't set)
endif

TEST_SERVERS = tests/unit/server/server tests/integration/server/server
TEST_CLIENTS = tests/unit/client/bin/native/9RIOT.elf \
	       tests/integration/client/bin/native/9RIOT.elf

test: $(TEST_CLIENTS) $(TEST_SERVERS) tests/run_tests.sh
	cd tests/ && ./run_tests.sh "$(TESTADDR)"

tests/unit/server/server: tests/unit/server/
	cd "$<" && ./build.sh
tests/integration/server/server: tests/integration/server/
	cd "$<" && ./build.sh

tests/unit/client/bin/native/9RIOT.elf: tests/unit/client/
	"$(MAKE)" -C $<
tests/integration/client/bin/native/9RIOT.elf: tests/integration/client/
	"$(MAKE)" -C $<

.PHONY: test
