#!/bin/sh

export GOPATH="$(pwd)"

if [ ! -d src ]; then
	mkdir -p src/github.com/nmeum/
	ln -s ../../../ src/github.com/nmeum/9RIOT
fi

cd src/github.com/nmeum/9RIOT
go build -o server
