export GOPATH=$(CURDIR)

server: src/github.com/Harvey-OS/ninep/cmd/ufs
	go build -o $@ $(patsubst src/%,%,$<)

.PHONY: server
