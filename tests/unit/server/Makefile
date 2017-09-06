export GOPATH=$(CURDIR)

server: src/github.com/nmeum/ninenano
	cd $< && go build -o $@

src/github.com/nmeum/ninenano: $(GOPATH)
	mkdir -p $(shell dirname $@)
	ln -s $< $@

.PHONY: server
