package main

import (
	"bufio"
	"flag"
	"github.com/Harvey-OS/ninep/protocol"
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
		log.Println("New control server connection")

		data, err := bufio.NewReader(cc).ReadString('\n')
		if err != nil {
			log.Printf("ReadAll: %v\n", err.Error())
			continue
		}

		req := data[:len(data)-1]
		log.Printf("Received control command %q\n", req)

		if cmd, ok := ctlcmds[req]; !ok {
			log.Println("Unknown control command")
		} else {
			reply = cmd
		}

		pc, err := pl.Accept()
		if err != nil {
			log.Printf("Accept: %v\n", err.Error())
			continue
		}
		log.Println("New protocol server connection")

		serv := NewServer(log.Printf, pc, pc)
		serv.Start()
	}
}
