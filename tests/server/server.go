package main

import (
	"bytes"
	"errors"
	"fmt"
	"github.com/Harvey-OS/ninep/protocol"
	"io"
)

type ServerFunc func(*bytes.Buffer) error
type ServerReply struct {
	fn ServerFunc
	ty protocol.MType
}

var reply ServerReply = ServerReply{
	func(b *bytes.Buffer) error {
		return errors.New("not implemented")
	},
	protocol.Tlast,
}

func RversionSuccess(b *bytes.Buffer) error {
	TMsize, TVersion, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, TVersion)
	return nil
}

func RversionUnknown(b *bytes.Buffer) error {
	TMsize, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, "unknown")
	return nil
}

func NewServer(d protocol.Tracer, t io.WriteCloser, f io.ReadCloser) *protocol.Server {
	s := new(protocol.Server)
	s.Trace = d
	s.FromNet = f
	s.ToNet = t
	s.Replies = make(chan protocol.RPCReply, protocol.NumTags)
	s.D = dispatch
	return s
}

func dispatch(s *protocol.Server, b *bytes.Buffer, t protocol.MType) error {
	if reply.ty == protocol.Tlast {
		return reply.fn(b)
	} else if t != reply.ty {
		return fmt.Errorf("Expected type %v - got %v", reply.ty, t)
	}

	return reply.fn(b)
}
