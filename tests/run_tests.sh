#!/bin/sh

export RIOTBASE="$(pwd)/../vendor/RIOT"

testsuite() {
	local name="${1}" log= pid=0 ret=0
	shift

	log="${name}.log"
	truncate -s 0 "${log}"

	printf "\n##\n# %s\n##\n\n" "Running ${name} tests"

	(./${name}/server/server $@ 1>"${log}" 2>&1) &
	pid=$!
	trap "kill ${pid}" INT

	cd ./${name}/client
	"${RIOTBASE}"/tests/unittests/tests/01-run.py
	ret=$?
	cd ./../..

	kill ${pid}
	trap - INT

	return ${ret}
}

if [ $# -ne 1 ]; then
	echo "USAGE: ${0##*/} ADDR" 2>&1
	exit 1
fi

export NINERIOT_ADDR="${1}"
export NINERIOT_CPORT="${NINERIOT_CPORT:-1338}"
export NINERIOT_PPORT="${NINERIOT_PPORT:-5543}"

testsuite unit \
	-ca ":${NINERIOT_CPORT}" \
	-pa ":${NINERIOT_PPORT}" \
	|| exit 1

testsuite integration \
	-ntype tcp \
	-addr ":${NINERIOT_PPORT}" \
	-root integration/server/testdata \
	|| exit 1
