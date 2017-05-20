#include <errno.h>
#include <byteorder.h>
#include <string.h>
#include <sys/types.h>

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
 * From version(5):
 *   The client suggests a maximum message size, msize, that is the
 *   maximum length, in bytes, it will ever generate or expect to
 *   receive in a single 9P message.
 *
 * The msize we are suggestion to the server is defined by the macro
 * ::_9P_MSIZE for the unlikly event that the server choosen an msize
 * smaller than the one we are suggesting we are storing the msize
 * actually used for the communication in this variable.
 *
 * It is declared with the initial value ::_9P_MSIZE to make it possible
 * to use this variable as an argument to ::sock_tcp_read even before a
 * session is established.
 */
static uint32_t msize = _9P_MSIZE;

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
 * Keep in mind that the same ::_9ppkt is being used for the T- and
 * R-message, thus after calling this method you shouldn't can't access
 * the R-message values anymore.
 *
 * @param p Pointer to the memory location containing the T-message. The
 *   caller doesn't need to initialize the `tag` field of this message.
 *   Besides this pointer is also used to store the R-message send by
 *   the server as a reply.
 * @return `0` on success, on error a negative errno is returned.
 */
static int
_do9p(_9ppkt *p)
{
	ssize_t ret; /* XXX should be a ssize_t */
	_9ptype ttype;
	uint16_t ttag;
	uint8_t head[_9P_HEADSIZ], *headpos;

	headpos = head;
	DEBUG("Sending message of type %d to server\n", p->type);

	/* From version(5):
	 *   The tag should be NOTAG (value (ushort)~0) for a version message.
	 */
	p->tag = (p->type == Tversion) ?
		_9P_NOTAG : random_uint32();

	/* Build the "header", meaning: size[4] type[1] tag[2]
	 * Every 9P message needs those first 7 bytes. */
	headpos = _htop32(headpos, p->len + _9P_HEADSIZ);
	headpos = _htop8(headpos, p->type);
	headpos = _htop16(headpos, p->tag);

	DEBUG("Sending %d bytes to server...\n", _9P_HEADSIZ + p->len);
	if ((ret = sock_tcp_write(&sock, head, _9P_HEADSIZ)) < 0)
		return ret;
	if (p->len > 0 && (ret = sock_tcp_write(&sock,
			p->buf, p->len)) < 0)
		return ret;

	DEBUG("Reading from server...\n");
	if ((ret = sock_tcp_read(&sock, buffer, msize, _9P_TIMOUT)) < 0)
		return ret;

	/* Tag and type will be overwritten by _9pheader. */
	ttype = p->type;
	ttag = p->tag;

	/* Maximum length of a 9P message is 2^32. */
	if ((unsigned)ret > UINT32_MAX) /* ret is >= 0 at this point. */
		return -EBADMSG;

	DEBUG("Read %d bytes from server, parsing them...\n", ret);
	if ((ret = _9pheader(p, (uint32_t)ret)))
		return ret;

	if (p->tag != ttag) {
		DEBUG("Tag mismatch (%d vs. %d)\n", p->tag, ttag);
		return -EBADMSG;
	}

	if (p->type != ttype + 1) {
		DEBUG("Unexpected value in type field: %d\n", p->type);
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
 * `_9P_MSIZE`. The msize choosen by the server is stored in the global
 * ::msize variable.
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
	_9ppkt pkt;

	bufpos = pkt.buf = buffer;

	/* From intro(5):
	 *   size[4] Tversion tag[2] msize[4] version[s]
	 */
	pkt.type = Tversion;
	bufpos = _htop32(bufpos, _9P_MSIZE);
	bufpos = _pstring(bufpos, _9P_VERSION);

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Rversion tag[2] msize[4] version[s]
	 *
	 * Also according to version(5) the version field in the
	 * R-message must be a string of the form `9Pnnnn` thus it has
	 * to be at least 4 bytes long plus 2 bytes for the size tag and
	 * 4 bytes for the msize field.
	 */
	if (pkt.len <= 10)
		return -EBADMSG;
	_ptoh32(&msize, &pkt);

	DEBUG("Msize of Rversion message: %d\n", msize);

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
	if (_hstring(ver, _9P_VERLEN, &pkt))
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
	_9ppkt pkt;

	bufpos = pkt.buf = buffer;

	/* From intro(5):
	 *   size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s]
	 */
	pkt.type = Tattach;
	bufpos = _htop32(bufpos, _9P_ROOTFID);
	bufpos = _htop32(bufpos, _9P_NOFID);
	bufpos = _pstring(bufpos, uname);
	bufpos = _pstring(bufpos, aname);

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		return NULL;

	/* From intro(5):
	 *   size[4] Rattach tag[2] qid[13]
	 */
	if (!(fid = _fidtbl(_9P_ROOTFID, ADD)))
		return NULL;
	fid->fid = _9P_ROOTFID;

	*fid->path = '\0'; /* empty string */
	if (_hqid(&fid->qid, &pkt))
		return NULL;

	return fid;
}

/**
 * From intro(5):
 *   The stat transaction retrieves information about the file. The stat
 *   field in the reply includes the file's name, access permissions
 *   (read, write and execute for owner, group and public), access and
 *   modification times, and owner and group identifications (see
 *   stat(2)).
 *
 * Retrieves information about the file associated with the given fid.
 *
 * @param fid Fid of the file to retrieve information for.
 * @param buf Pointer to stat struct to fill.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pstat(_9pfid *fid, struct stat *b)
{
	int r;
	uint8_t *bufpos;
	uint32_t mode;
	uint16_t nstat;
	_9ppkt pkt;

	bufpos = pkt.buf = buffer;

	/* From intro(5):
	 *   size[4] Tstat tag[2] fid[4]
	 */
	pkt.type = Tstat;
	bufpos = _htop32(bufpos, fid->fid);

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		return -1;

	/* From intro(5):
	 *   size[4] Rstat tag[2] stat[n]
	 *
	 * See stat(5) for the definition of stat[n].
	 */
	if (pkt.len < _9P_STATSIZ)
		return -EBADMSG;

	/* From intro(5):
	 *   The notation parameter[n] where n is not a constant
	 *   represents a variable-length parameter: n[2] followed by n
	 *   bytes of data forming the parameter.
	 *
	 * The nstat value should be equal to pkt.len at this point.
	 */
	_ptoh16(&nstat, &pkt);

	DEBUG("nstat: %d\n", nstat);
	if (nstat != pkt.len)
		return -EBADMSG;

	/* Skip: n[2], size[2], type[2] and dev[4]. */
	pkt.buf += 2 * BIT16SZ + BIT32SZ;
	pkt.len -= 2 * BIT16SZ + BIT32SZ;

	/* store qid informations in given fid. */
	_ptoh8(&fid->qid.type, &pkt);
	_ptoh32(&fid->qid.vers, &pkt);
	_ptoh64(&fid->qid.path, &pkt);

	/* store the other information in the stat struct. */
	_ptoh32(&mode, &pkt);
	b->st_mode = (mode & DMDIR) ? S_IFDIR : S_IFREG;
	_ptoh32((uint32_t*)&b->st_atime, &pkt);
	_ptoh32((uint32_t*)&b->st_mtime, &pkt);
	b->st_ctime = b->st_mtime;
	_ptoh64((uint64_t*)&b->st_size, &pkt);

	/* information for stat struct we cannot extract from the reply. */
	b->st_dev = b->st_ino = b->st_rdev = 0;
	b->st_nlink = 1;
	b->st_uid = b->st_gid = 0;
	b->st_blksize = msize - _9P_IOHDRSIZ;
	b->st_blocks = b->st_size / b->st_blksize + 1;

	/* extract the file name and store it in the fid. */
	if (_hstring(fid->path, _9P_PTHMAX, &pkt))
		return -EBADMSG;

	/* uid, gid and muid are ignored. */

	return 0;
}
