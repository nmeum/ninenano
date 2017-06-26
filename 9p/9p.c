#include <assert.h>
#include <errno.h>
#include <byteorder.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include "xtimer.h"
#include "random.h"
#include "9p.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

/**
 * @defgroup Static utility functions.
 *
 * @{
 */

/**
 * Initializes the members of a packet buffer. The length field is set
 * to the amount of bytes still available in the buffer and should be
 * decremented when writing to the buffer.
 *
 * @param ctx 9P connection context.
 * @param pkt Pointer to a packet for which the members should be
 * 	initialized.
 * @param type Type which should be used for this packet.
 */
void
newpkt(_9pctx *ctx, _9ppkt *pkt, _9ptype type)
{
	pkt->buf = ctx->buffer + _9P_HEADSIZ;
	pkt->len = ctx->msize - _9P_HEADSIZ;
	pkt->type = type;
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
 * @param ctx 9P connection context.
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
_9pheader(_9pctx *ctx, _9ppkt *pkt, uint32_t buflen)
{
	uint8_t type;
	uint32_t len;

	pkt->buf = ctx->buffer;

	/* From intro(5):
	 *   Each 9P message begins with a four-byte size field
	 *   specifying the length in bytes of the complete message
	 *   including the four bytes of the size field itself.
	 */
	if (buflen < BIT32SZ)
		return -EBADMSG;
	ptoh32(&len, pkt);

	DEBUG("Length of the 9P message: %"PRIu32"\n", len);
	if (len > buflen || len < _9P_HEADSIZ)
		return -EBADMSG;
	pkt->len = len - BIT32SZ;

	/* From intro(5):
	 *   The next byte is the message type, one of the constants in
	 *   the enumeration in the include file <fcall.h>.
	 */
	ptoh8(&type, pkt);

	DEBUG("Type of 9P message: %"PRIu8"\n", type);
	if (type < Tversion || type >= Tmax)
		return -EBADMSG;
	if (type % 2 == 0)
		return -ENOTSUP; /* Client only implementation */
	pkt->type = (_9ptype)type;

	/* From intro(5):
	 *   The next two bytes are an identifying tag, described below.
	 */
	ptoh16(&pkt->tag, pkt);
	DEBUG("Tag of 9P message: %"PRIu16"\n", pkt->tag);

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
 * The length field of the given packet should be equal to the amount of
 * space (in bytes) still available in the packet buffer.
 *
 * @param ctx 9P connection context.
 * @param p Pointer to the memory location containing the T-message. The
 *   caller doesn't need to initialize the `tag` field of this message.
 *   Besides this pointer is also used to store the R-message send by
 *   the server as a reply.
 * @return `0` on success, on error a negative errno is returned.
 */
static int
do9p(_9pctx *ctx, _9ppkt *p)
{
	ssize_t ret;
	_9ptype ttype;
	uint32_t reallen;
	uint16_t ttag;

	DEBUG("Sending message of type %d to server\n", p->type);

	/* From version(5):
	 *   The tag should be NOTAG (value (ushort)~0) for a version message.
	 */
	p->tag = (p->type == Tversion) ?
		_9P_NOTAG : random_uint32();

	p->buf = ctx->buffer; /* Reset buffer position. */
	reallen = ctx->msize - p->len;

	/* Build the "header", meaning: size[4] type[1] tag[2]
	 * Every 9P message needs those first 7 bytes. */
	htop32(reallen, p);
	htop8(p->type, p);
	htop16(p->tag, p);

	DEBUG("Sending %"PRIu32" bytes to server...\n", reallen);
	if ((ret = ctx->write(ctx->buffer, reallen)) < 0)
		return ret;

	/* Tag and type will be overwritten by _9pheader. */
	ttype = p->type;
	ttag = p->tag;

	DEBUG("Reading from server...\n");
	if ((ret = ctx->read(ctx->buffer, ctx->msize)) < 0)
		return ret;

	/* Maximum length of a 9P message is 2^32. */
	if ((size_t)ret > UINT32_MAX)
		return -EMSGSIZE;

	DEBUG("Read %zu bytes from server, parsing them...\n", ret);
	if ((ret = _9pheader(ctx, p, (uint32_t)ret)))
		return ret;

	if (p->tag != ttag) {
		DEBUG("Tag mismatch (%"PRIu8" vs. %"PRIu8")\n", p->tag, ttag);
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
 * @param ctx 9P connection context.
 * @param f Pointer to fid on which the operation should be performed.
 * @param t Type of the operation which should be performed.
 *
 * @return `0` on success, on error a negative errno is returned.
 */
static int
fidrem(_9pctx *ctx, _9pfid *f, _9ptype t)
{
	int r;
	_9ppkt pkt;

	assert(t == Tclunk || t == Tremove);

	/* From intro(5):
	 *   size[4] Tclunk|Tremove tag[2] fid[4]
	 */
	newpkt(ctx, &pkt, t);
	htop32(f->fid, &pkt);

	if ((r = do9p(ctx, &pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Rclunk|Rremove tag[2]
	 *
	 * These first seven bytes are already parsed by do9p.
	 * Therefore we don't need to parse anything here.
	 */

	if (!fidtbl(ctx->fids, f->fid, DEL))
		return -EBADF;

	return 0;
}

/**
 * Parses the body of a 9P Ropen or Rcreate message. The body of those
 * two messages consists of a qid and an iounit. The values of both
 * fields are stored in the given fid.
 *
 * @param ctx 9P connection context.
 * @param f Pointer to the fid associated with the new file.
 * @param pkt Pointer to the packet from which the body should be read.
 * @return `0` on success, on error a negative errno is returned.
 */
static int
newfile(_9pctx *ctx, _9pfid *f, _9ppkt *pkt)
{
	if (hqid(&f->qid, pkt) || pkt->len < BIT32SZ)
		return -EBADMSG;
	ptoh32(&f->iounit, pkt);

	/* From open(5):
	 *   The iounit field returned by open and create may be zero.
	 *   If it is not, it is the maximum number of bytes that are
	 *   guaranteed to be read from or written to the file without
	 *   breaking the I/O transfer into multiple 9P messages
	 */
	if (!f->iounit)
		f->iounit = ctx->msize - _9P_IOHDRSIZ;

	f->off = 0;
	return 0;
}

/**
 * Only a maximum of bytes can be transfered atomically. If the amonut
 * of bytes we want to write to a file (or read from it) exceed this
 * limit we need to send multiple R-messages to the server. This
 * function takes care of doing this.
 *
 * @pre t == Twrite || t == Tread
 *
 * @param ctx 9P connection context.
 * @param f Pointer to fid on which a read or write operation should be
 * 	performed.
 * @param buf Pointer to a buffer from which data should be written to
 * 	or written from.
 * @param count Amount of bytes that should be written or read from the
 * 	file.
 * @param t Type of the operation which should be performed.
 */
static int
ioloop(_9pctx *ctx, _9pfid *f, char *buf, size_t count, _9ptype t)
{
	int r;
	size_t n;
	uint32_t pcnt, ocnt;
	_9ppkt pkt;

	assert(t == Twrite || t == Tread);

	n = 0;
	while (n < count) {
		/* From intro(5):
		 *   size[4] Tread tag[2] fid[4] offset[8] count[4]
		 *   size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
		 */
		newpkt(ctx, &pkt, t);
		htop32(f->fid, &pkt);
		htop64(f->off, &pkt);

		if (count - n <= UINT32_MAX)
			pcnt = count - n;
		else
			pcnt = UINT32_MAX;

		if (pcnt > f->iounit)
			pcnt = f->iounit;
		htop32(pcnt, &pkt);

		if (t == Twrite) {
			if (pcnt > pkt.len - BIT32SZ)
				pcnt = pkt.len - BIT32SZ;
			if (!pcnt) return -EOVERFLOW;
			bufcpy(&pkt, &buf[n], pcnt);
		}

		DEBUG("Sending %s with offset %"PRIu64" and count %"PRIu32"\n",
			(t == Tread) ? "Tread" : "Twrite", f->off, pcnt);

		if ((r = do9p(ctx, &pkt)))
			return r;

		/* From intro(5):
		 *   size[4] Rread tag[2] count[4] data[count]
		 *   size[4] Rwrite tag[2] count[4]
		 */
		ocnt = pcnt;
		if (pkt.len < BIT32SZ)
			return -EBADMSG;
		ptoh32(&pcnt, &pkt);

		/* From open(5):
		 *   If the offset field is greater than or equal to the
		 *   number of bytes in the file, a count of zero will
		 *   be returned.
		 */
		if (!pcnt)
			return 0; /* EOF */

		if (pcnt > count)
			return -EBADMSG;

		if (t == Tread) {
			if (pkt.len < pcnt)
				return -EBADMSG;
			memcpy(&buf[n], pkt.buf, pcnt);
		}

		n += pcnt;
		f->off += pcnt;

		if (pcnt < ocnt)
			break;
	}

	return n;
}

/**@}*/

/**
 * Initializes a 9P connection context.
 *
 * @param read Function used for receiving data from the server.
 * @param write Function used for sending data to the server.
 * @param ctx 9P connection context which should be initialized.
 */
void
_9pinit(_9pctx *ctx, iofunc *read, iofunc *write)
{
	random_init(xtimer_now().ticks32);
	memset(ctx->fids, 0, _9P_MAXFIDS * sizeof(_9pfid));

	ctx->msize = _9P_MSIZE;
	ctx->write = write;
	ctx->read = read;
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
 * @param ctx 9P connection context.
 *
 * @return `0` on success.
 * @return `-EBADMSG` if the 9P message was invalid.
 * @return `-EMSGSIZE` if the server msize was greater than `_9P_MSIZE`.
 * @return `-ENOPROTOOPT` server implements a different version of the
 *   9P network protocol.
 */
int
_9pversion(_9pctx *ctx)
{
	int r;
	char ver[_9P_VERLEN];
	_9ppkt pkt;

	/* From intro(5):
	 *   size[4] Tversion tag[2] msize[4] version[s]
	 */
	newpkt(ctx, &pkt, Tversion);
	htop32(_9P_MSIZE, &pkt);
	if (pstring(_9P_VERSION, &pkt))
		return -EOVERFLOW;

	if ((r = do9p(ctx, &pkt)))
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
	ptoh32(&ctx->msize, &pkt);

	DEBUG("Msize of Rversion message: %"PRIu32"\n", ctx->msize);

	/* From version(5):
	 *   The server responds with its own maximum, msize, which must
	 *   be less than or equal to the client's value.
	 */
	if (ctx->msize > _9P_MSIZE) {
		DEBUG("Servers msize is too large (%"PRIu32")\n", ctx->msize);
		return -EMSGSIZE;
	} else if (ctx->msize < _9P_MINSIZE) {
		DEBUG("Servers msize is too small (%"PRIu32")\n", ctx->msize);
		return -EOVERFLOW;
	}

	/* From version(5):
	 *  If the server does not understand the client's version
	 *  string, it should respond with an Rversion message (not
	 *  Rerror) with the version string the 7 characters `unknown`.
         */
	if (hstring(ver, _9P_VERLEN, &pkt))
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
 * @param ctx 9P connection context.
 * @param dest Pointer to a pointer which should be set to the address
 *   of the corresponding entry in the fid table.
 * @param uname User identification.
 * @param aname File tree to access.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pattach(_9pctx *ctx, _9pfid **dest, char *uname, char *aname)
{
	int r;
	_9pfid *fid;
	_9ppkt pkt;

	/* From intro(5):
	 *   size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s]
	 */
	newpkt(ctx, &pkt, Tattach);
	htop32(_9P_ROOTFID, &pkt);
	htop32(_9P_NOFID, &pkt);
	if (pstring(uname, &pkt) || pstring(aname, &pkt))
		return -EOVERFLOW;

	if ((r = do9p(ctx, &pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Rattach tag[2] qid[13]
	 */
	if (!(fid = fidtbl(ctx->fids, _9P_ROOTFID, ADD)))
		return -ENFILE;
	fid->fid = _9P_ROOTFID;

	if (hqid(&fid->qid, &pkt)) {
		fid->fid = 0; /* mark fid as free. */
		return -EBADMSG;
	}

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
 * @param ctx 9P connection context.
 * @param f Pointer to a fid which should be closed.
 * @return `0` on success, on error a negative errno is returned.
 */
inline int
_9pclunk(_9pctx *ctx, _9pfid *f)
{
	return fidrem(ctx, f, Tclunk);
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
 * @param ctx 9P connection context.
 * @param fid Fid of the file to retrieve information for.
 * @param buf Pointer to stat struct to fill.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pstat(_9pctx *ctx, _9pfid *fid, struct stat *b)
{
	int r;
	uint32_t mode;
	_9ppkt pkt;

	/* From intro(5):
	 *   size[4] Tstat tag[2] fid[4]
	 */
	newpkt(ctx, &pkt, Tstat);
	htop32(fid->fid, &pkt);

	if ((r = do9p(ctx, &pkt)))
		return -1;

	/* From intro(5):
	 *   size[4] Rstat tag[2] stat[n]
	 *
	 * See stat(5) for the definition of stat[n].
	 */
	if (pkt.len < _9P_STATSIZ)
		return -EBADMSG;

	/* Skip: n[2], size[2], type[2] and dev[4]. */
	advbuf(&pkt, 3 * BIT16SZ + BIT32SZ);

	/* store qid informations in given fid. */
	if (hqid(&fid->qid, &pkt))
		return -EBADMSG;

	/* store the other information in the stat struct. */
	ptoh32(&mode, &pkt);
	b->st_mode = (mode & DMDIR) ? S_IFDIR : S_IFREG;
	ptoh32((uint32_t*)&b->st_atime, &pkt);
	ptoh32((uint32_t*)&b->st_mtime, &pkt);
	b->st_ctime = b->st_mtime;
	ptoh64((uint64_t*)&b->st_size, &pkt);

	/* information for stat struct we cannot extract from the reply. */
	b->st_dev = b->st_ino = b->st_rdev = 0;
	b->st_nlink = 1;
	b->st_uid = b->st_gid = 0;
	b->st_blksize = ctx->msize - _9P_IOHDRSIZ;
	b->st_blocks = b->st_size / b->st_blksize + 1;

	/* name, uid, gid and muid are ignored. */

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
 * @param ctx 9P connection context.
 * @param dest Pointer to a pointer which should be set to the address
 *   of the corresponding entry in the fid table.
 * @param path Path which should be walked.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9pwalk(_9pctx *ctx, _9pfid **dest, char *path)
{
	int r;
	char *cur, *sep;
	size_t n, i, len;
	uint8_t *nwname;
	uint16_t nwqid;
	ptrdiff_t elen;
	_9pfid *fid;
	_9ppkt pkt;

	if (*path == '\0' || !strcmp(path, "/"))
		len = 0;
	else
		len = strlen(path);

	if (!(fid = newfid(ctx->fids)))
		return -ENFILE;

	/* From intro(5):
	 *   size[4] Twalk tag[2] fid[4] newfid[4] nwname[2]
	 *   nwname*(wname[s])
	 */
	newpkt(ctx, &pkt, Twalk);
	htop32(_9P_ROOTFID, &pkt);
	htop32(fid->fid, &pkt);

	/* leave space for nwname[2]. */
	nwname = pkt.buf;
	advbuf(&pkt, BIT16SZ);

	/* generate nwname*(wname[s]) */
	for (n = i = 0; i < len; n++, i += elen + 1) {
		if (n >= _9P_MAXWEL) {
			r = -ENAMETOOLONG;
			goto err;
		}

		cur = &path[i];
		if (!(sep = strchr(cur, _9P_PATHSEP)))
			sep = &path[len - 1] + 1; /* XXX */

		elen = sep - cur;
		if ((size_t)elen > pkt.len - BIT32SZ) {
			r = -EOVERFLOW;
			goto err;
		}

		htop16(elen, &pkt);
		bufcpy(&pkt, cur, elen);
	}

	DEBUG("Constructed Twalk with %zu elements\n", n);

	pkt.buf = nwname;
	pkt.len += BIT16SZ;
	htop16(n, &pkt);

	if ((r = do9p(ctx, &pkt)))
		goto err;

	/* From intro(5):
	 *   size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
	 */
	if (pkt.len < BIT16SZ) {
		r = -EBADMSG;
		goto err;
	}
	ptoh16(&nwqid, &pkt);

	/**
	 * From walk(5):
	 *   nwqid is therefore either nwname or the index of the first
	 *   elementwise walk that failed.
	 */
	DEBUG("nwqid: %"PRIu16"\n", nwqid);
	if (nwqid != n || nwqid > pkt.len || nwqid > _9P_MAXWEL) { /* XXX */
		r = -EBADMSG;
		goto err;
	}

	/* Retrieve the last qid. */
	advbuf(&pkt, (nwqid - 1) * _9P_QIDSIZ);
	if (hqid(&fid->qid, &pkt)) {
		r = -EBADMSG;
		goto err;
	}

	*dest = fid;
	return 0;

err:
	assert(fidtbl(ctx->fids, fid->fid, DEL) != NULL);
	return r;
}

/**
 * From open(5):
 *   The open request asks the file server to check permissions and
 *   prepare a fid for I/O with subsequent read and write messages.
 *
 * @param ctx 9P connection context.
 * @param f Fid which should be opened for I/O.
 * @param flags Flags used for opening the fid. Supported flags are:
 * 	OREAD, OWRITE, ORDWR and OTRUNC.
 * @return `0` on success, on error a negative errno is returned.
 */
int
_9popen(_9pctx *ctx, _9pfid *f, int flags)
{
	int r;
	_9ppkt pkt;

	/* From intro(5):
	 *   size[4] Topen tag[2] fid[4] mode[1]
	 */
	newpkt(ctx, &pkt, Topen);
	htop32(f->fid, &pkt);
	htop8(flags, &pkt);

	if ((r = do9p(ctx, &pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Ropen tag[2] qid[13] iounit[4]
	 */
	if ((r = newfile(ctx, f, &pkt)))
		return r;

	return 0;
}

/**
 * From open(5):
 *   The create request asks the file server to create a new file with the name
 *   supplied, in the directory (dir) represented by fid, and requires write
 *   permission in the directory
 *
 * After creation the file will be opened automatically. And the given
 * fid will no longer represent the directory, instead it now represents
 * the newly created file.
 *
 * @param ctx 9P connection context.
 * @param f Pointer to the fid associated with the directory in which a
 * 	new file should be created.
 * @param name Name which should be used for the newly created file.
 * @param perm Permissions with which the new file should be created.
 * @param flags Flags which should be used for opening the file
 *   afterwards. See ::_9popen.
 */
int
_9pcreate(_9pctx *ctx, _9pfid *f, char *name, int perm, int flags)
{
	int r;
	_9ppkt pkt;

	/* From intro(5):
	 *   size[4] Tcreate tag[2] fid[4] name[s] perm[4] mode[1]
	 */
	newpkt(ctx, &pkt, Tcreate);
	htop32(f->fid, &pkt);
	if (pstring(name, &pkt) || BIT32SZ + BIT8SZ > pkt.len)
		return -EOVERFLOW;
	htop32(perm, &pkt);
	htop8(flags, &pkt);

	if ((r = do9p(ctx, &pkt)))
		return r;

	/* From intro(5):
	 *   size[4] Rcreate tag[2] qid[13] iounit[4]
	 */
	if ((r = newfile(ctx, f, &pkt)))
		return r;

	return 0;
}

/**
 * From read(5):
 *   The read request asks for count bytes of data from the file
 *   identified by fid, which must be opened for reading, start-
 *   ing offset bytes after the beginning of the file.
 *
 * @param ctx 9P connection context.
 * @param f Fid from which data should be read.
 * @param dest Pointer to a buffer to which the received data should be
 * 	written.
 * @param count Amount of data that should be read.
 * @return The number of bytes read on success or a negative errno on
 * 	error.
 */
inline ssize_t
_9pread(_9pctx *ctx, _9pfid *f, char *dest, size_t count)
{
	return ioloop(ctx, f, dest, count, Tread);
}

/**
 * From intro(5):
 *   The write request asks that count bytes of data be recorded in the
 *   file identified by fid, which must be opened for writing, starting
 *   offset bytes after the beginning of the file.
 *
 * @param ctx 9P connection context.
 * @param f Pointer to the fid to which data should be written.
 * @param src Pointer to a buffer containing the data which should be
 * 	written to the file.
 * @param count Amount of bytes which should be written to the file
 * @return The number of bytes read on success or a negative errno on
 * 	error.
 */
ssize_t
_9pwrite(_9pctx *ctx, _9pfid *f, char *src, size_t count)
{
	return ioloop(ctx, f, src, count, Twrite);
}

/**
 * From remove(5):
 *   The remove request asks the file server both to remove the file
 *   represented by fid and to clunk the fid, even if the remove fails.
 *
 * @param ctx 9P connection context.
 * @param f Pointer to the fid which should be removed.
 * @return `0` on success, on error a negative errno is returned.
 */
inline int
_9premove(_9pctx *ctx, _9pfid *f)
{
	return fidrem(ctx, f, Tremove);
}
