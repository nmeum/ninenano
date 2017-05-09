#include <errno.h>
#include <byteorder.h>
#include <string.h>

#include "xtimer.h"
#include "random.h"
#include "9pfs.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

/**
 * Global static buffer used for storing message specific parameters.
 * Message indepented parameters are stored on the stack using `_9ppkt`.
 *
 * The functions writting to this buffer `_htop{8,16, 32, 64}` and so
 * assume that there is always enough space available and (for
 * performance reasons) do no perform any boundary checks when writting
 * to it.
 */
static uint8_t buffer[_9P_MSIZE];

/**
 * Global Sock TCP socket used for communicating with the 9P server.
 *
 * We use sock here to be independent of the underlying network stack.
 * Currently only the lwip network stack has support for socks's TCP API
 * but that will hopefully change in the future.
 */
static sock_tcp_t sock;

/**
 * As with file descriptors, we need to store currently open fids
 * somehow. This is done in this static buffer. This buffer should only
 * be accessed using the ::_fidtbl function it should never be modified
 * directly.
 *
 * This buffer is not static because it is used in the ::_fidtbl
 * function from `util.c`.
 */
_9pfid fids[_9P_MAXFIDS];

/**
 * Initializes the 9P module. Among other things it opens a TCP socket
 * but it does not initiate the connection with the server. This has to
 * be done manually using the ::_9pversion and ::_9pattach functions.
 *
 * @param remote Remote address of the server to connect to.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pinit(sock_tcp_ep_t remote)
{
	int r;

	random_init(xtimer_now().ticks32);

	DEBUG("Connecting to TCP socket...\n");
	if ((r = sock_tcp_connect(&sock, &remote, 0, SOCK_FLAGS_REUSE_EP)))
		return r;

	return 0;
}

/**
 * Closes an existing 9P connection freeing all resources on the server
 * and the client.
 */
void
_9pclose(void)
{
	memset(fids, '\0', _9P_MAXFIDS);
	sock_tcp_disconnect(&sock);
}

/**
 * Parses the header (the first 7 bytes) of a 9P message contained in
 * the given buffer. The result is written to the memory area pointed to
 * by the `_9ppkt` buffer.
 *
 * The buflen parameter is a `uint32_t` and not a `size_t` because each
 * 9P message begins with a four-byte size field specifying the size of
 * the entire 9P message. Thus the message length will never exceed `2^32`
 * bytes.
 *
 * @param pkt Pointer to memory area to store result in.
 * @param buf Buffer containg the message which should be parsed.
 * @param buflen Total length of the given buffer.
 * @return `0` on success.
 * @return `-EBADMSG` if the buffer content isn't a valid 9P message.
 * @return `-ENOTSUP` if the message type isn't supported.
 *   This is a client implementation thus we only support R-messages,
 *   T-messages are not supported and yield this error code.
 */
static int
_9pheader(_9ppkt *pkt, uint32_t buflen)
{
	uint8_t type;

	pkt->buf = buffer;

	/* From intro(5):
	 *   Each 9P message begins with a four-byte size field
	 *   specifying the length in bytes of the complete message
	 *   including the four bytes of the size field itself.
	 */
	if (buflen < BIT32SZ)
		return -EBADMSG;
	_ptoh32(&pkt->len, pkt);

	DEBUG("Length of the 9P message: %d\n", pkt->len);
	if (pkt->len > buflen || pkt->len < _9P_HEADSIZ)
		return -EBADMSG;

	/* From intro(5):
	 *   The next byte is the message type, one of the constants in
	 *   the enumeration in the include file <fcall.h>.
	 */
	_ptoh8(&type, pkt);

	DEBUG("Type of 9P message: %d\n", type);
	if (type < Tversion || type >= Tmax)
		return -EBADMSG;
	if (type % 2 == 0)
		return -ENOTSUP; /* Client only implementation */
	pkt->type = (_9ptype)type;

	/* From intro(5):
	 *   The next two bytes are an identifying tag, described below.
	 */
	_ptoh16(&pkt->tag, pkt);
	DEBUG("Tag of 9P message: %d\n", pkt->tag);

	return 0;
}

/**
 * Performs a 9P request, meaning this function sends a T-message to the
 * server, reads the R-message from the client and stores it in a memory
 * location provided by the caller.
 *
 * Keep in mind that the R- and T-messages use the same buffer for the
 * message specific parameter, thus after calling this method you
 * shouldn't read from the R-message buffer anymore.
 *
 * @param t T-message which should be send to the 9P server. The caller
 *   doesn't need to initialize the `tag` field since this is
 *   initialized by this function.
 * @param r Pointer to a memory location where the server response
 *   (the R-message) should be stored.
 * @return `0` on success, on error a negative errno is returned.
 */
static int
_do9p(_9ppkt *t, _9ppkt *r)
{
	int ret; /* XXX should be a ssize_t */
	uint8_t head[_9P_HEADSIZ], *headpos;

	headpos = head;
	DEBUG("Sending message of type %d to server\n", t->type);

	/* From version(5):
	 *   The tag should be NOTAG (value (ushort)~0) for a version message.
	 */
	t->tag = (t->type == Tversion) ?
		_9P_NOTAG : random_uint32();

	/* Build the "header", meaning: size[4] type[1] tag[2]
	 * Every 9P message needs those first 7 bytes. */
	headpos = _htop32(headpos, t->len + _9P_HEADSIZ);
	headpos = _htop8(headpos, t->type);
	headpos = _htop16(headpos, t->tag);

	DEBUG("Sending %d bytes to server...\n", _9P_HEADSIZ + t->len);
	if ((ret = sock_tcp_write(&sock, head, _9P_HEADSIZ)) < 0)
		return ret;
	if (t->len > 0 && (ret = sock_tcp_write(&sock,
			t->buf, t->len)) < 0)
		return ret;

	DEBUG("Reading from server...\n");
	if ((ret = sock_tcp_read(&sock, buffer,
			_9P_MSIZE, SOCK_NO_TIMEOUT)) < 0)
		return ret;

	DEBUG("Read %d bytes from server, parsing them...\n", ret);
	if ((ret = _9pheader(r, (uint32_t)ret)))
		return ret;

	if (r->tag != t->tag) {
		DEBUG("Tag mismatch (%d vs. %d)\n", r->tag, t->tag);
		return -EBADMSG;
	}

	if (r->type != t->type + 1) {
		DEBUG("Unexpected value in type field: %d\n", r->type);
		return -EBADMSG;
	}

	return 0;
}

/**
 * From version(5):
 *   The version request negotiates the protocol version and message
 *   size to be used on the connection and initializes the connection
 *   for I/O. Tversion must be the first message sent on the 9P
 *   connection, and the client cannot issue any further requests until
 *   it has received the Rversion reply.
 *
 * The version parameter is always set to the value of `_9P_VERSION`,
 * the msize parameter on the other hand is always set to the value of
 * `_9P_MSIZE`.
 *
 * @return `0` on success.
 * @return `-EBADMSG` if the 9P message was invalid.
 * @return `-EMSGSIZE` if the server msize was greater than `_9P_MSIZE`.
 * @return `-ENOPROTOOPT` server implements a different version of the
 *   9P network protocol.
 */
int
_9pversion(void)
{
	int r;
	char ver[_9P_VERLEN];
	uint8_t *bufpos;
	uint32_t msize;
	_9ppkt tver, rver;

	bufpos = tver.buf = buffer;

	/* From intro(5):
	 *   size[4] Tversion tag[2] msize[4] version[s]
	 */
	tver.type = Tversion;
	bufpos = _htop32(bufpos, _9P_MSIZE);
	bufpos = _pstring(bufpos, _9P_VERSION);

	tver.len = bufpos - tver.buf;
	if ((r = _do9p(&tver, &rver)))
		return r;

	/* From intro(5):
	 *   size[4] Rversion tag[2] msize[4] version[s]
	 *
	 * Also according to version(5) the version field in the
	 * R-message must be a string of the form `9Pnnnn` thus it has
	 * to be at least 4 bytes long plus 2 bytes for the size tag and
	 * 4 bytes for the msize field.
	 */
	if (rver.len <= 10)
		return -EBADMSG;
	_ptoh32(&msize, &rver);

	DEBUG("msize of Rversion message: %d\n", msize);

	/* From version(5):
	 *   The server responds with its own maximum, msize, which must
	 *   be less than or equal to the client's value.
	 */
	if (msize > _9P_MSIZE) {
		DEBUG("Servers msize is too large (%d)\n", msize);
		return -EMSGSIZE;
	}

	/* From version(5):
	 *  If the server does not understand the client's version
	 *  string, it should respond with an Rversion message (not
	 *  Rerror) with the version string the 7 characters `unknown`.
         */
        if (_hstring(ver, _9P_VERLEN, &rver))
        	return -EBADMSG;

	DEBUG("Version string reported by server: %s\n", ver);
	if (!strcmp(ver, "unknown"))
		return -ENOPROTOOPT;

	return 0;
}

/**
 * From attach(5):
 *   The attach message serves as a fresh introduction from a user on
 *   the client machine to the server. As a result of the attach
 *   transaction, the client will have a connection to the root
 *   directory of the desired file tree, represented by fid.
 *
 * The afid parameter is always set to the value of `_9P_NOFID` since
 * authentication is not supported currently.
 *
 * @param uname User identification.
 * @param aname File tree to access.
 * @return Pointer to the location of the fib table entry associated
 *   with the qid returned by the server or NULL on failure.
 */
_9pfid*
_9pattach(char *uname, char *aname)
{
	int r;
	uint8_t *bufpos;
	_9pfid *fid;
	_9ppkt tatt, ratt;

	bufpos = tatt.buf = buffer;

	/* From intro(5):
	 *   size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s]
	 */
	tatt.type = Tattach;
	bufpos = _htop32(bufpos, _9P_ROOTFID);
	bufpos = _htop32(bufpos, _9P_NOFID);
	bufpos = _pstring(bufpos, uname);
	bufpos = _pstring(bufpos, aname);

	tatt.len = bufpos - tatt.buf;
	if ((r = _do9p(&tatt, &ratt)))
		return NULL;

	/* From intro(5):
	 *   size[4] Rattach tag[2] qid[13]
	 */
	if (!(fid = _fidtbl(_9P_ROOTFID, ADD)))
		return NULL;
	fid->fid = _9P_ROOTFID;

	fid->path = "";
	if (_hqid(&fid->qid, &ratt))
		return NULL;

	return fid;
}
