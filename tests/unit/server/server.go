package main

import (
	"bytes"
	"errors"
	"fmt"
	"github.com/Harvey-OS/ninep/protocol"
	"io"
)

// Handles an incoming T-message send by the client.
type ServerFunc func(*bytes.Buffer) error

// Contains an expected T-message type and the function which should be
// called when this type is encountered.
type ServerReply struct {
	fn ServerFunc
	ty protocol.MType
}

// Reply which should be used when an unknown control command was read
// from the control socket.
var failureReply ServerReply = ServerReply{
	func(b *bytes.Buffer) error {
		return errors.New("not implemented")
	},
	protocol.Tlast,
}

// Channel containing the reply to be used by the protocol connection
// handler. The goal is to make sure that the protocol connection
// handler doesn't access it before it has been set by the control
// connection handler.
var replyChan = make(chan ServerReply)

// Creates a new ninep protocol server. The new server reads T-messages from
// the given ReadCloser and writes R-messages to the given WriteCloser.
// Debug information is created by calling the given tracer.
func NewServer(d protocol.Tracer, t io.WriteCloser, f io.ReadCloser) *protocol.Server {
	s := new(protocol.Server)
	s.Trace = d
	s.FromNet = f
	s.ToNet = t
	s.Replies = make(chan protocol.RPCReply, protocol.NumTags)
	s.D = dispatch
	return s
}

// Dispatches an incoming T-message using the current ServerReply. If a
// server reply wasn't set or if the T-message send but the client
// doesn't match the ServerReply message type an error is returned.
// Besides an error may be returned if the current ServerReply handler
// returns an error.
func dispatch(s *protocol.Server, b *bytes.Buffer, t protocol.MType) error {
	reply := <-replyChan
	if reply.ty == protocol.Tlast {
		return reply.fn(b)
	} else if t != reply.ty {
		return fmt.Errorf("Expected type %v - got %v", reply.ty, t)
	}

	return reply.fn(b)
}
