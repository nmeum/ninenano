package main

import (
	"bytes"
	"errors"
	"github.com/Harvey-OS/ninep/protocol"
)

// Maps strings written by the client to the control socket to
// server replies. Every test function needs an entry in this table.
var ctlcmds = map[string]ServerReply{
	"header_too_short1":    {HeaderTooShort1, protocol.Tversion},
	"header_too_short2":    {HeaderTooShort2, protocol.Tversion},
	"header_too_large":     {HeaderTooLarge, protocol.Tversion},
	"header_wrong_type":    {HeaderWrongType, protocol.Tversion},
	"header_invalid_type":  {HeaderInvalidType, protocol.Tversion},
	"header_tag_mismatch":  {HeaderTagMismatch, protocol.Tversion},
	"header_type_mismatch": {HeaderTypeMismatch, protocol.Tversion},

	"rversion_success":          {RversionSuccess, protocol.Tversion},
	"rversion_unknown":          {RversionUnknown, protocol.Tversion},
	"rversion_msize_too_big":    {RversionMsizeTooBig, protocol.Tversion},
	"rversion_invalid":          {RversionInvalidVersion, protocol.Tversion},
	"rversion_invalid_len":      {RversionInvalidLength, protocol.Tversion},
	"rversion_version_too_long": {RversionVersionTooLong, protocol.Tversion},

	"rattach_success":     {RattachSuccess, protocol.Tattach},
	"rattach_invalid_len": {RattachInvalidLength, protocol.Tattach},

	"rstat_success":       {RstatSuccess, protocol.Tstat},
	"rstat_nstat_invalid": {RstatNstatInvalid, protocol.Tstat},

	"rwalk_success":         {RwalkSuccess, protocol.Twalk},
	"rwalk_invalid_len":     {RwalkInvalidLen, protocol.Twalk},
	"rwalk_nwqid_too_large": {RwalkNwqidTooLarge, protocol.Twalk},

	"ropen_success": {RopenSuccess, protocol.Topen},

	"rread_success":     {RreadSuccess, protocol.Tread},
	"rread_with_offset": {RreadWithOffset, protocol.Tread},
	"rread_count_zero":  {RreadCountZero, protocol.Tread},
}

// Replies with a single byte. This is thus even shorter than the four-byte
// length field and should not be parsed by the client succesfully.
func HeaderTooShort1(b *bytes.Buffer) error {
	b.Reset()
	b.Write([]byte{0})
	return nil
}

// Replies with a message containing a four-byte size field with a value
// that is too small to make the message a valid 9p message.
func HeaderTooShort2(b *bytes.Buffer) error {
	b.Reset()
	l := uint64(6)
	b.Write([]byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	return nil
}

// Replies with a length field that is larger than the actual amount of bytes
// send to the client.
func HeaderTooLarge(b *bytes.Buffer) error {
	b.Reset()

	l := uint64(42)
	b.Write([]byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	return nil
}

// Replies with a message containing a T-message type field.
func HeaderWrongType(b *bytes.Buffer) error {
	_, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	b.Reset()
	b.Write([]byte{0, 0, 0, 0,
		uint8(protocol.Tversion),
		byte(t), byte(t >> 8),
		byte(0), byte(0), byte(0), byte(0)})

	{
		l := uint64(b.Len())
		copy(b.Bytes(), []byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	}

	return nil
}

// Replies with an invalid type value. The client should not be able to parse
// this successfully.
func HeaderInvalidType(b *bytes.Buffer) error {
	_, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	b.Reset()
	b.Write([]byte{0, 0, 0, 0,
		uint8(protocol.Tlast),
		byte(t), byte(t >> 8),
		byte(0), byte(0), byte(0), byte(0)})

	return nil
}

// Replies with a tag mismatched tag message. The client should be able
// to parse this but should raise an error.
func HeaderTagMismatch(b *bytes.Buffer) error {
	_, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	t += 1

	b.Reset()
	b.Write([]byte{0, 0, 0, 0,
		uint8(protocol.Rversion),
		byte(t), byte(t >> 8),
		byte(0), byte(0), byte(0), byte(0)})

	return nil
}

// Replies with a type message that is valid but not expected by the
// client. Thus the client should be able to parse this but should raise
// an error.
func HeaderTypeMismatch(b *bytes.Buffer) error {
	_, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	b.Reset()
	b.Write([]byte{0, 0, 0, 0,
		uint8(protocol.Rversion),
		byte(t), byte(t >> 8),
		byte(0), byte(0), byte(0), byte(0)})

	return nil
}

// Replies with the msize and version send by the client. This should always be
// successfully parsed by the client implementation.
func RversionSuccess(b *bytes.Buffer) error {
	TMsize, TVersion, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, TVersion)
	return nil
}

// Replies with the version string "unknown".
//
// From version(5):
//   If the server does not understand the client's version
//   string, it should respond with an Rversion message (not
//   Rerror) with the version string the 7 characters
//   ``unknown''.
//
// The client should therefore be able to parse this message but should
// return an error.
func RversionUnknown(b *bytes.Buffer) error {
	TMsize, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, "unknown")
	return nil
}

// Replies with an msize value equal to the one send by the client plus
// one.
//
// From version(5):
//   The server responds with its own maximum, msize, which must
//   be less than or equal to the client's value.
//
// The client should therefore be able to parse this message but should
// return an error.
func RversionMsizeTooBig(b *bytes.Buffer) error {
	TMsize, TVersion, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize+1, TVersion)
	return nil
}

// Replies with an invalid version string.
//
// From version(5):
//  After stripping any such period-separated suffix, the server is
//  allowed to respond with a string of the form 9Pnnnn, where nnnn is
//  less than or equal to the digits sent by the client.
//
// Depending on the client implementation the client may not be able to
// parse the R-message. In any case it should return an error since the
// he shouldn't support this version of the 9P protocol.
func RversionInvalidVersion(b *bytes.Buffer) error {
	TMsize, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, "9P20009P2000")
	return nil
}

// Replies with an length field in the version R-message.
//
// The value of the size field is one byte too short. Thereby causing
// the length field of the version string to report a string size that
// would exceeds the size of the packet itself.
//
// The client should not be able to parse this R-message.
func RversionInvalidLength(b *bytes.Buffer) error {
	TMsize, TVersion, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, TVersion)

	{
		var l uint64 = uint64(b.Len() - 1)
		copy(b.Bytes(), []byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	}

	return nil
}

// Replies with a version string that is one byte longer than the
// longest valid version string `unknown`.
//
// The client will most likely be able to parse this messages but might
// reject it if it exceeds a statically allocated buffer.
func RversionVersionTooLong(b *bytes.Buffer) error {
	TMsize, _, t, err := protocol.UnmarshalTversionPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRversionPkt(b, t, TMsize, "12345678")
	return nil
}

// Successfully attaches the client. Replying with a vaild qid. The
// client should be able to parse this successfully.
func RattachSuccess(b *bytes.Buffer) error {
	_, _, _, _, t, err := protocol.UnmarshalTattachPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRattachPkt(b, t, protocol.QID{})
	return nil
}

// Replies with a modified packet length field causing the packet length to be
// one byte shorter than neccessary. The qid should therefore be invalind and
// the client should not be able to parse this.
func RattachInvalidLength(b *bytes.Buffer) error {
	_, _, _, _, t, err := protocol.UnmarshalTattachPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRattachPkt(b, t, protocol.QID{})

	{
		var l uint64 = uint64(b.Len() - 1)
		copy(b.Bytes(), []byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	}

	return nil
}

// Replies with a valid Rstat message. The client must be able to parse
// this R-message.
func RstatSuccess(b *bytes.Buffer) error {
	_, t, err := protocol.UnmarshalTstatPkt(b)
	if err != nil {
		return err
	}

	qid := protocol.QID{
		Type:    23,
		Version: 2342,
		Path:    1337,
	}

	dir := protocol.Dir{
		Type:    9001,
		Dev:     5,
		QID:     qid,
		Mode:    protocol.DMDIR,
		Atime:   1494443596,
		Mtime:   1494443609,
		Length:  2342,
		Name:    "testfile",
		User:    "testuser",
		Group:   "testgroup",
		ModUser: "ken",
	}

	var B bytes.Buffer
	protocol.Marshaldir(&B, dir)

	protocol.MarshalRstatPkt(b, t, B.Bytes())
	return nil
}

// Replies with a stat message containing an invalid two-byte nstat
// field which would cause the message to be longer than indicated in
// the size field.
func RstatNstatInvalid(b *bytes.Buffer) error {
	_, t, err := protocol.UnmarshalTstatPkt(b)
	if err != nil {
		return err
	}

	var B bytes.Buffer
	var D protocol.Dir

	protocol.Marshaldir(&B, D)

	var l uint64
	var n uint16 = uint16(1337)

	b.Reset()
	b.Write([]byte{0, 0, 0, 0,
		uint8(protocol.Rstat),
		byte(t), byte(t >> 8),
		uint8(n >> 0),
		uint8(n >> 8),
	})

	b.Write(B.Bytes())

	{
		l = uint64(b.Len())
		copy(b.Bytes(), []byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	}

	return nil
}

// Replies with a valid Rwalk message. The last QID is different then
// the other ones to test off-by-one errors. The client must be able
// to parse this R-message.
func RwalkSuccess(b *bytes.Buffer) error {
	_, _, p, t, err := protocol.UnmarshalTwalkPkt(b)
	if err != nil {
		return err
	}
	plen := len(p)

	q := protocol.QID{
		Path:    23,
		Type:    42,
		Version: 5,
	}

	var qids []protocol.QID

	for i := 0; i < plen; i++ {
		qids = append(qids, q)
	}

	qids[plen-1] = protocol.QID{
		Path:    1337,
		Type:    23,
		Version: 42,
	}

	protocol.MarshalRwalkPkt(b, t, qids)
	return nil
}

// Replies with a message that is too short to be a valid Rwalk message.
// The client should therefore reject this message.
func RwalkInvalidLen(b *bytes.Buffer) error {
	_, _, p, t, err := protocol.UnmarshalTwalkPkt(b)
	if err != nil {
		return err
	}

	var qids []protocol.QID
	for i := 0; i < len(p); i++ {
		qids = append(qids, protocol.QID{})
	}

	protocol.MarshalRwalkPkt(b, t, qids)

	{
		var l uint64 = uint64(10)
		copy(b.Bytes(), []byte{uint8(l), uint8(l >> 8), uint8(l >> 16), uint8(l >> 24)})
	}

	return nil
}

// Replies with a Rwalk message which contains an `nwqid` field that
// exceeds MAXWELEM and should therefore be considered invalid by the
// client.
func RwalkNwqidTooLarge(b *bytes.Buffer) error {
	_, _, _, t, err := protocol.UnmarshalTwalkPkt(b)
	if err != nil {
		return err
	}

	var qids []protocol.QID
	for i := 0; i < 17; i++ {
		qids = append(qids, protocol.QID{})
	}

	protocol.MarshalRwalkPkt(b, t, qids)
	return nil
}

// Replies with a valid Ropen message. The iounit of the reply will
// always be `1337`. The client must be able to parse this.
func RopenSuccess(b *bytes.Buffer) error {
	_, _, t, err := protocol.UnmarshalTopenPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRopenPkt(b, t, protocol.QID{}, 1337)
	return nil
}

// Replies with a valid Rread message. The data field of the message
// should hold the string `Hello!`. The client must be able to parse
// this. This function ignores the count sent by the client.
func RreadSuccess(b *bytes.Buffer) error {
	_, _, _, t, err := protocol.UnmarshalTreadPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRreadPkt(b, t, []byte("Hello!"))
	return nil
}

// Replies with a valid Rread message. The data field of the message is
// set to `1234567890`. Contrary to RreadSuccess this function respects
// the offset and count send by the client.
func RreadWithOffset(b *bytes.Buffer) error {
	_, o, l, t, err := protocol.UnmarshalTreadPkt(b)
	if err != nil {
		return err
	}

	poff := int(o)
	plen := int(l)

	hstr := "1234567890"
	hlen := len(hstr)

	if poff > hlen {
		return errors.New("offset is too large")
	}
	if plen > hlen {
		hlen = plen
	}

	data := hstr[poff : poff+plen]

	protocol.MarshalRreadPkt(b, t, []byte(data))
	return nil
}

// Replies with a valid Rread message which contains a count field with
// the value `0`. The client must be able to parse this but should
// treat the message as an error.
func RreadCountZero(b *bytes.Buffer) error {
	_, _, _, t, err := protocol.UnmarshalTreadPkt(b)
	if err != nil {
		return err
	}

	protocol.MarshalRreadPkt(b, t, []byte(""))
	return nil
}
