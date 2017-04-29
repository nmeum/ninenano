package main

import (
	"flag"
	"github.com/Harvey-OS/ninep/protocol"
	"io/ioutil"
	"log"
	"net"
)

var (
	caddr = flag.String("ca", ":2342", "Control server network address")
	paddr = flag.String("pa", ":4223", "9P server network address")
)

var ctlcmds = map[string]ServerReply{
	"test_rversion_success": {RversionSuccess, protocol.Tversion},
}

func main() {
	cl, err := net.Listen("tcp", *caddr)
	if err != nil {
		panic(err)
	}

	pl, err := net.Listen("tcp", *paddr)
	if err != nil {
		panic(err)
	}

	for {
		cc, err := cl.Accept()
		if err != nil {
			log.Printf("Accept: %v\n", err.Error())
			continue
		}

		data, err := ioutil.ReadAll(cc)
		if err != nil {
			log.Printf("ReadAll: %v\n", err.Error())
			continue
		}

		req := string(data)
		if cmd, ok := ctlcmds[req]; !ok {
			log.Printf("Unknown control command %q\n", req)
		} else {
			reply = cmd
		}

		pc, err := pl.Accept()
		if err != nil {
			log.Printf("Accept: %v\n", err.Error())
			continue
		}

		serv := NewServer(pc, pc)
		serv.Start()
	}
}
