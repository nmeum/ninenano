#include <errno.h>
#include <byteorder.h>
#include <string.h>
#include <fcntl.h>
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
 * @attention The functions writting to this buffer `_htop{8,16, 32,
 * 64}` and so assume that there is always enough space available and
 * (for performance reasons) do no perform any boundary checks when
 * writting to it.
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
	memset(fids, '\0', _9P_MAXFIDS * sizeof(_9pfid));
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
	uint32_t len;

	pkt->buf = buffer;

	/* From intro(5):
	 *   Each 9P message begins with a four-byte size field
	 *   specifying the length in bytes of the complete message
	 *   including the four bytes of the size field itself.
	 */
	if (buflen < BIT32SZ)
		return -EBADMSG;
	_ptoh32(&len, pkt);

	DEBUG("Length of the 9P message: %zu\n", len);
	if (len > buflen || len < _9P_HEADSIZ)
		return -EBADMSG;
	pkt->len = len - BIT32SZ;

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
	ssize_t ret;
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

	DEBUG("Sending %zu bytes to server...\n", _9P_HEADSIZ + p->len);
	if ((ret = sock_tcp_write(&sock, head, _9P_HEADSIZ)) < 0)
		return ret;
	if (p->len > 0 && (ret = sock_tcp_write(&sock,
			p->buf, p->len)) < 0)
		return ret;

	/* Tag and type will be overwritten by _9pheader. */
	ttype = p->type;
	ttag = p->tag;

	DEBUG("Reading from server...\n");
	if ((ret = sock_tcp_read(&sock, buffer, msize, _9P_TIMOUT)) < 0)
		return ret;

	/* Maximum length of a 9P message is 2^32. */
	if ((unsigned)ret > UINT32_MAX) /* ret is >= 0 at this point. */
		return -EBADMSG;

	DEBUG("Read %zu bytes from server, parsing them...\n", ret);
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
 * Frees all resources allocated for a given fid on both the client and
 * the server. Optionally the file associated with the fid can also be
 * removed from the server.
 *
 * @pre t == Tclunk || t == Tremove
 *
 * @param f Pointer to fid on which the operation should be performed.
 * @param t Type of the operation which should be performed.
 *
 * @return `0` on success, on error a negative errno is returned.
 */
static int
_fidrem(_9pfid *f, _9ptype t)
{
	int r;
	uint8_t *bufpos;
	_9ppkt pkt;

	if (t != Tclunk && t != Tremove)
		return -EINVAL;
	bufpos = pkt.buf = buffer;

	/* From intro(5):
	 *   size[4] Tclunk|Tremove tag[2] fid[4]
	 */
	pkt.type = t;
	bufpos = _htop32(bufpos, f->fid);

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Rclunk|Rremove tag[2]
	 *
	 * These first seven bytes are already parsed by _do9p.
	 * Therefore we don't need to parse anything here.
	 */

	if (!_fidtbl(f->fid, DEL))
		return -EBADF;

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
 * @param dest Pointer to a pointer which should be set to the address
 *   of the corresponding entry in the fid table.
 * @param uname User identification.
 * @param aname File tree to access.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pattach(_9pfid **dest, char *uname, char *aname)
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
		return r;

	/* From intro(5):
	 *   size[4] Rattach tag[2] qid[13]
	 */
	if (!(fid = _fidtbl(_9P_ROOTFID, ADD)))
		return -ENFILE;
	fid->fid = _9P_ROOTFID;

	if (_hqid(&fid->qid, &pkt)) {
		fid->fid = 0; /* mark fid as free. */
		return -EBADMSG;
	}

	*fid->path = '\0'; /* empty string */

	*dest = fid;
	return 0;
}

/**
 * From clunk(5):
 *   The clunk request informs the file server that the current file
 *   represented by fid is no longer needed by the client. The actual
 *   file is not removed on the server unless the fid had been opened
 *   with ORCLOSE.
 *
 * @param f Pointer to a fid which should be closed.
 * @return `0` on success, on error a negative errno is returned.
 */
inline int
_9pclunk(_9pfid *f)
{
	return _fidrem(f, Tclunk);
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

	/* Skip: n[2], size[2], type[2] and dev[4]. */
	ADVBUF(&pkt, 3 * BIT16SZ + BIT32SZ);

	/* store qid informations in given fid. */
	if (_hqid(&fid->qid, &pkt))
		return -EBADMSG;

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

/* TODO insert _9pwstat implementation here. */

/**
 * From intro(5):
 *   A walk message causes the server to change the current file
 *   associated with a fid to be a file in the directory that is the old
 *   current file, or one of its subdirectories. Walk returns a new fid
 *   that refers to the resulting file. Usually, a client maintains a
 *   fid for the root, and navigates by walks from the root fid.
 *
 * This function alsways walks frmom the root fid and returns a fid for
 * the last element of the given path.
 *
 * @attention The 9P protocol specifies that only a a maximum of sixteen
 * name elements or qids may be packed in a single message.  For name
 * elements or qids that exceeds this limit multiple Twalk/Rwalk message
 * need to be send. Since `VFS_NAME_MAX` defaults to `31` it is very
 * unlikely that this limit will be reached. Therefore this function
 * doesn't support sending walk messages that exceed this limit.
 *
 * @param dest Pointer to a pointer which should be set to the address
 *   of the corresponding entry in the fid table.
 * @param path Path which should be walked.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pwalk(_9pfid **dest, char *path)
{
	int r;
	char *cur, *sep;
	size_t n, i, len;
	uint8_t *bufpos, *nwname;
	uint16_t nwqid;
	ptrdiff_t elen;
	_9pfid *fid;
	_9ppkt pkt;

	if (*path == '\0' || !strcmp(path, "/"))
		return -EINVAL; /* TODO */
	bufpos = pkt.buf = buffer;

	len = strlen(path);
	if (len >= _9P_PTHMAX)
		return -EINVAL;
	if (!(fid = newfid()))
		return -ENFILE;

	/* From intro(5):
	 *   size[4] Twalk tag[2] fid[4] newfid[4] nwname[2]
	 *   nwname*(wname[s])
	 */
	pkt.type = Twalk;
	bufpos = _htop32(bufpos, _9P_ROOTFID);
	bufpos = nwname = _htop32(bufpos, fid->fid);
	bufpos += 2; /* leave space for nwname[2]. */

	/* generate nwname*(wname[s]) */
	for (n = i = 0; i < len; n++, i += elen + 1) {
		if (n >= _9P_MAXWEL) {
			r = -EINVAL;
			goto err;
		}

		cur = &path[i];
		if (!(sep = strchr(cur, '/')))
			sep = &path[len - 1] + 1; /* XXX */
		elen = sep - cur;

		/* XXX this is a duplication of the _pstring func. */
		bufpos = _htop16(bufpos, elen);
		memcpy(bufpos, cur, elen);
		bufpos += elen;
	}

	DEBUG("Constructed Twalk with %d elements\n", n);
	_htop16(nwname, n); /* nwname[2] */

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		goto err;

	/* From intro(5):
	 *   size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
	 */
	if (pkt.len < BIT16SZ) {
		r = -EBADMSG;
		goto err;
	}
	_ptoh16(&nwqid, &pkt);

	/**
	 * From walk(5):
	 *   nwqid is therefore either nwname or the index of the first
	 *   elementwise walk that failed.
	 */
	DEBUG("nwqid: %d\n", nwqid);
	if (nwqid != n || nwqid > pkt.len || nwqid > _9P_MAXWEL) { /* XXX */
		r = -EBADMSG;
		goto err;
	}

	/* Retrieve the last qid. */
	ADVBUF(&pkt, (nwqid - 1) * _9P_QIDSIZ);
	if (_hqid(&fid->qid, &pkt)) {
		r = -EBADMSG;
		goto err;
	}

	/* Copy string, overflow check is performed above. */
	strcpy(fid->path, path);

	*dest = fid;
	return 0;

err:
	assert(_fidtbl(fid->fid, DEL) != NULL);
	return r;
}

/**
 * From open(5):
 *   The open request asks the file server to check permissions and
 *   prepare a fid for I/O with subsequent read and write messages.
 *
 * @param f Fid which should be opened for I/O.
 * @param flags Flags used for opening the fid. Supported flags are:
 * 	OREAD, OWRITE, ORDWR and OTRUNC.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9popen(_9pfid *f, int flags)
{
	int r;
	uint8_t *bufpos;
	_9ppkt pkt;

	bufpos = pkt.buf = buffer;

	/* From intro(5):
	 *   size[4] Topen tag[2] fid[4] mode[1]
	 */
	pkt.type = Topen;
	bufpos = _htop32(bufpos, f->fid);
	bufpos = _htop8(bufpos, flags);

	pkt.len = bufpos - pkt.buf;
	if ((r = _do9p(&pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Ropen tag[2] qid[13] iounit[4]
	 */
	if (_hqid(&f->qid, &pkt) || pkt.len < BIT32SZ)
		return -EBADMSG;
	_ptoh32(&f->iounit, &pkt);

	/* From open(5):
	 *   The iounit field returned by open and create may be zero.
	 *   If it is not, it is the maximum number of bytes that are
	 *   guaranteed to be read from or written to the file without
	 *   breaking the I/O transfer into multiple 9P messages
	 */
	if (!f->iounit)
		f->iounit = msize - _9P_IOHDRSIZ;

	return 0;
}

/**
 * From read(5):
 *   The read request asks for count bytes of data from the file
 *   identified by fid, which must be opened for reading, start-
 *   ing offset bytes after the beginning of the file.
 *
 * @param f Fid from which data should be read.
 * @param dest Pointer to a buffer to which the received data should be
 * 	written.
 * @param count Amount of data that should be read.
 * @return The number of bytes read on success or a negative
 * 	errno on error.
 */
ssize_t
_9pread(_9pfid *f, char *dest, size_t count)
{
	int r;
	size_t n;
	uint32_t pcnt, ocnt;
	uint8_t *bufpos;
	_9ppkt pkt;

	n = 0;
	while (n < count) {
		bufpos = pkt.buf = buffer;
		DEBUG("Sending Tread with offset %zu\n", n);

		/* From intro(5):
		 *   size[4] Tread tag[2] fid[4] offset[8] count[4]
		 */
		pkt.type = Tread;
		bufpos = _htop32(bufpos, f->fid);
		bufpos = _htop64(bufpos, n);

		if (count - n <= UINT32_MAX)
			pcnt = count - n;
		else
			pcnt = UINT32_MAX;

		if (pcnt > f->iounit)
			pcnt = f->iounit;
		bufpos = _htop32(bufpos, pcnt);

		ocnt = pcnt;
		DEBUG("Requesting %zu bytes from the server\n", pcnt);

		pkt.len = bufpos - pkt.buf;
		if ((r = _do9p(&pkt)))
			return r;

		/* From intro(5):
		 *   size[4] Rread tag[2] count[4] data[count]
		 */
		if (pkt.len < BIT32SZ)
			return -EBADMSG;
		_ptoh32(&pcnt, &pkt);

		DEBUG("Received %zu bytes from the server\n", pcnt);
		if (pkt.len < pcnt || pcnt > count)
			return -EBADMSG;

		/* From open(5):
		 *   If the offset field is greater than or equal to the
		 *   number of bytes in the file, a count of zero will
		 *   be returned.
		 */
		if (!pcnt)
			return -EFBIG;

		memcpy(&dest[n], pkt.buf, pcnt);

		n += pcnt;
		if (pcnt < ocnt)
			break;
	}

	return n;
}

/**
 * From remove(5):
 *   The remove request asks the file server both to remove the file
 *   represented by fid and to clunk the fid, even if the remove fails.
 *
 * @param f Pointer to the fid which should be removed.
 * @return `0` on success, on error a negative errno is returned.
 */
inline int
_9premove(_9pfid *f)
{
	return _fidrem(f, Tremove);
}
