// Package main implements a simple 9P server which can be used to test
// 9P client implementations. It opens two TCP sockets on given ports.
// The first socket is the control socket, the client must send a
// test name delimited by a newline character to this socket before
// connecting to the second socket (the 9P protocol socket).
//
// After sending the test name to the server the client can connect to
// the 9P protocol socket. The server accepts T-messages on this port
// and (depending on the test name) responses with R-messages.
package main

import (
	"bufio"
	"flag"
	"log"
	"net"
)

var (
	caddr = flag.String("ca", ":2342", "Control server network address")
	paddr = flag.String("pa", ":4223", "9P server network address")
)

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
