#!/bin/sh

export GOPATH="$(pwd)"
go build -o server github.com/Harvey-OS/ninep/cmd/ufs
