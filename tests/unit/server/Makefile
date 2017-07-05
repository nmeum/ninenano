export GOPATH=$(CURDIR)

server: src/github.com/nmeum/9RIOT
	cd $< && go build -o $@

src/github.com/nmeum/9RIOT: $(GOPATH)
	mkdir -p $(shell dirname $@)
	ln -s $< $@

.PHONY: server
