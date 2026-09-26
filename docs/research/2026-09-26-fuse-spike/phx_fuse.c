/* SPDX-License-Identifier: BSD-3-Clause -- host-only spike for the FUSE feasibility study (2026-09-26) */
/*
 * SPIKE: Phoenix-RTOS transport for OpenBSD's libfuse (ISC).
 *
 * Replaces OpenBSD lib/libfuse/{fuse_session.c,fuse_lowlevel.c,fuse_chan.c}
 * (the /dev/fuse0 fusebuf decode/encode layer). OpenBSD's fuse.c (patched
 * fuse_mount) and fuse_ops.c/fuse_subr.c/tree.c/dict.c/fuse_opt.c are reused
 * unmodified: fuse_ops.c already turns inode-based low-level requests into
 * path-based struct fuse_operations calls. This file turns Phoenix msg_t
 * requests into those low-level requests.
 *
 * Host mock: phx_recv()/phx_respond() stand in for msgRecv()/msgRespond().
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>

#include "fuse_private.h"
#include "phx_msg.h"

/* PROPOSED new message (does not exist in Phoenix today): atomic rename.
 * oid = old dir, i.ln.oid = new dir, i.data = "oldname\0newname\0". */
#define mtRename 0xf55	/* next free in libphoenix sys/file.h range (mtMountPoint = 0xf54) */

extern int phx_recv(struct fuse_chan *ch, msg_t *msg, int *rid);
extern void phx_respond(struct fuse_chan *ch, msg_t *msg, int rid);
extern uint32_t phx_port(struct fuse_chan *ch);

enum { R_NONE = 0, R_ERR, R_ENTRY, R_ATTR, R_OPEN, R_BUF, R_WRITE, R_STATFS };

struct phx_reply {
	int kind;
	int err;			/* positive errno, 0 = ok */
	struct fuse_entry_param entry;
	struct stat attr;
	struct fuse_file_info fi;
	struct statvfs stv;
	char *dst;			/* where reply_buf/readlink copy to */
	size_t dstlen;
	size_t len;			/* bytes copied / written */
};

/* ---------- per-oid open handle (Phoenix has no per-open file handle) ---- */

struct phx_handle {
	fuse_ino_t ino;
	unsigned int refs;
	int isdir;
	struct fuse_file_info fi;
	struct phx_handle *next;
};

/* ---------- per-dir listing snapshot for mtReaddir (1 entry per msg) ----- */

struct phx_dent { uint64_t ino; uint32_t type; char *name; };
struct phx_dirsnap {
	fuse_ino_t ino;
	struct phx_dent *ents;
	size_t n;
};

static struct {
	struct phx_handle *handles;
	struct phx_dirsnap snap;	/* one snapshot at a time, like nfs */
	struct fuse_session *se;
} phx;

/* ---------------- low-level reply API (called by fuse_ops.c) ------------ */

static void rep_set(fuse_req_t req, int kind, int err)
{
	req->rep->kind = kind;
	req->rep->err = err;
}

int fuse_reply_err(fuse_req_t req, int err) { rep_set(req, R_ERR, err); return 0; }
void fuse_reply_none(fuse_req_t req) { rep_set(req, R_NONE, 0); }

int fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param *e)
{
	req->rep->entry = *e;
	rep_set(req, R_ENTRY, 0);
	return 0;
}

int fuse_reply_attr(fuse_req_t req, const struct stat *st, double tmo)
{
	(void)tmo;
	req->rep->attr = *st;
	rep_set(req, R_ATTR, 0);
	return 0;
}

int fuse_reply_open(fuse_req_t req, const struct fuse_file_info *fi)
{
	req->rep->fi = *fi;
	rep_set(req, R_OPEN, 0);
	return 0;
}

int fuse_reply_buf(fuse_req_t req, const char *buf, off_t size)
{
	struct phx_reply *r = req->rep;
	size_t n = (size_t)size < r->dstlen ? (size_t)size : r->dstlen;

	if (n > 0)
		memcpy(r->dst, buf, n);	/* copy: caller frees buf on return */
	r->len = n;
	rep_set(req, R_BUF, 0);
	return 0;
}

int fuse_reply_readlink(fuse_req_t req, char *path)
{
	return fuse_reply_buf(req, path, strlen(path));
}

int fuse_reply_write(fuse_req_t req, size_t n)
{
	req->rep->len = n;
	rep_set(req, R_WRITE, 0);
	return 0;
}

int fuse_reply_statfs(fuse_req_t req, const struct statvfs *st)
{
	req->rep->stv = *st;
	rep_set(req, R_STATFS, 0);
	return 0;
}

size_t fuse_add_direntry(fuse_req_t req, char *buf, const size_t bufsize,
    const char *name, const struct stat *st, off_t off)
{
	struct fuse_dirent *d;
	size_t namelen, len;

	if (name == NULL)
		return 0;
	namelen = strlen(name);
	len = FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + namelen);
	if (buf == NULL || st == NULL || req == NULL || bufsize < len)
		return len;
	d = (struct fuse_dirent *)buf;
	memset(d, 0, len);
	d->ino = st->st_ino;
	d->type = IFTODT(st->st_mode);
	d->off = off;
	d->namelen = namelen;
	memcpy(d->name, name, namelen);
	return len;
}

const struct fuse_ctx *fuse_req_ctx(fuse_req_t req) { return &req->ctx; }
void *fuse_req_userdata(fuse_req_t req) { return req->se->userdata; }

/* ---------------- session API ------------------------------------------ */

struct fuse_session *fuse_lowlevel_new(struct fuse_args *args,
    const struct fuse_lowlevel_ops *ops, const size_t size, void *userdata)
{
	struct fuse_session *se = calloc(1, sizeof(*se));

	(void)args;
	if (se == NULL)
		return NULL;
	memcpy(&se->llops, ops, size < sizeof(se->llops) ? size : sizeof(se->llops));
	se->userdata = userdata;
	se->fci.proto_major = 7;
	se->fci.proto_minor = 9;
	se->fci.max_write = 128 * 1024;
	se->fci.max_readahead = 0;
	return se;
}

void fuse_session_add_chan(struct fuse_session *se, struct fuse_chan *ch) { se->chan = ch; ch->se = se; }
void fuse_session_remove_chan(struct fuse_chan *ch) { if (ch->se) ch->se->chan = NULL; ch->se = NULL; }
void fuse_session_exit(struct fuse_session *se) { se->exit = 1; }
int fuse_session_exited(const struct fuse_session *se) { return se->exit; }
void fuse_session_reset(struct fuse_session *se) { se->exit = 0; }
int fuse_chan_fd(struct fuse_chan *ch) { (void)ch; return -1; }

void fuse_session_destroy(struct fuse_session *se)
{
	if (se->init && se->llops.destroy)
		se->llops.destroy(se->userdata);
	free(se);
}

/* ---------------- helpers ---------------------------------------------- */

static int phx_ll(struct fuse_session *se, struct phx_reply *r)
{
	(void)se;
	return r->kind == R_ERR ? -r->err : 0;
}

#define LL(op, ...) do {						\
	memset(&rep, 0, sizeof(rep)); rep.dst = dst; rep.dstlen = dstlen;	\
	if (se->llops.op == NULL) { err = -ENOSYS; break; }		\
	se->llops.op(&req, __VA_ARGS__);				\
	err = phx_ll(se, &rep);						\
} while (0)

static int phx_otype(mode_t m)
{
	if (S_ISDIR(m)) return otDir;
	if (S_ISREG(m)) return otFile;
	if (S_ISLNK(m)) return otSymlink;
	return otDev;
}

static struct phx_handle *h_find(fuse_ino_t ino)
{
	struct phx_handle *h;
	for (h = phx.handles; h != NULL; h = h->next)
		if (h->ino == ino)
			return h;
	return NULL;
}

static void snap_drop(void)
{
	for (size_t i = 0; i < phx.snap.n; i++)
		free(phx.snap.ents[i].name);
	free(phx.snap.ents);
	memset(&phx.snap, 0, sizeof(phx.snap));
}

static uint32_t dt2ot(uint32_t dt)
{
	switch (dt) {
	case DT_DIR: return otDir;
	case DT_REG: return otFile;
	case DT_LNK: return otSymlink;
	default: return otDev;
	}
}

/* ---------------- the dispatcher: one Phoenix msg -> low-level op(s) ---- */

static void phx_dispatch(struct fuse_session *se, msg_t *msg)
{
	struct phx_reply rep;
	struct fuse_req req = { .rep = &rep, .se = se, .ch = se->chan };
	uint32_t port = phx_port(se->chan);
	fuse_ino_t ino = msg->oid.id;	/* oid.id == FUSE nodeid; root = 1 */
	char *dst = NULL;
	size_t dstlen = 0;
	int err = 0;

	req.ctx.pid = msg->pid;		/* Phoenix passes no uid/gid */

	switch (msg->type) {
	case mtLookup: {
		const char *name = msg->i.data;
		size_t len = 0;
		fuse_ino_t cur = ino;

		while (name[len] != '\0') {
			char comp[NAME_MAX + 1];
			size_t n;

			while (name[len] == '/')
				len++;
			if (name[len] == '\0')
				break;
			for (n = 0; name[len + n] != '\0' && name[len + n] != '/'; n++)
				;
			if (n > NAME_MAX) { err = -ENAMETOOLONG; break; }
			memcpy(comp, name + len, n);
			comp[n] = '\0';
			/* TODO: ".." at our root must return the parent fs oid, and
			 * names that are mountpoints/device nodes spliced in via
			 * mtSetAttr(atDev)/mtCreate(otDev) must be served from a local
			 * table (see nfs_ops_lookup) -- the FUSE fs cannot store oids. */
			if (strcmp(comp, ".") != 0) {
				LL(lookup, cur, comp);
				if (err < 0)
					break;
				cur = rep.entry.ino;
			}
			len += n;
		}
		if (err == 0) {
			msg->o.lookup.fil.port = port;
			msg->o.lookup.fil.id = cur;
			msg->o.lookup.dev = msg->o.lookup.fil;
			err = (int)len;
		}
		break;
	}

	case mtGetAttr:
	case mtGetAttrAll: {
		struct stat *st = &rep.attr;
		LL(getattr, ino, NULL);
		if (err < 0)
			break;
		if (msg->type == mtGetAttrAll) {
			struct _attrAll *a = msg->o.data;
			if (a == NULL || msg->o.size < sizeof(*a)) { err = -EINVAL; break; }
			memset(a, 0, sizeof(*a));
			a->mode.val = st->st_mode; a->uid.val = st->st_uid;
			a->gid.val = st->st_gid; a->size.val = st->st_size;
			a->blocks.val = st->st_blocks; a->ioblock.val = st->st_blksize;
			a->type.val = phx_otype(st->st_mode); a->port.val = port;
			a->cTime.val = st->st_ctime; a->mTime.val = st->st_mtime;
			a->aTime.val = st->st_atime; a->links.val = st->st_nlink;
			a->dev.val = st->st_dev;
			a->pollStatus.val = 0x5; /* POLLIN|POLLOUT (regular files) */
		}
		else {
			long long v = 0;
			switch (msg->i.attr.type) {
			case atMode: v = st->st_mode; break;
			case atUid: v = st->st_uid; break;
			case atGid: v = st->st_gid; break;
			case atSize: v = st->st_size; break;
			case atBlocks: v = st->st_blocks; break;
			case atIOBlock: v = st->st_blksize; break;
			case atType: v = phx_otype(st->st_mode); break;
			case atPort: v = port; break;
			case atCTime: v = st->st_ctime; break;
			case atMTime: v = st->st_mtime; break;
			case atATime: v = st->st_atime; break;
			case atLinks: v = st->st_nlink; break;
			default: err = -EINVAL; break;
			}
			msg->o.attr.val = v;
		}
		break;
	}

	case mtOpen: {
		struct phx_handle *h = h_find(ino);
		if (h != NULL) { h->refs++; break; }
		LL(getattr, ino, NULL);
		if (err < 0)
			break;
		h = calloc(1, sizeof(*h));
		h->ino = ino;
		h->isdir = S_ISDIR(rep.attr.st_mode);
		/* One handle per oid, shared by every opener: per-open flags
		 * (O_APPEND, O_RDONLY vs O_RDWR) cannot be honoured. */
		h->fi.flags = h->isdir ? O_RDONLY : O_RDWR;
		if (h->isdir)
			LL(opendir, ino, &h->fi);
		else
			LL(open, ino, &h->fi);
		if (err < 0) { free(h); break; }
		h->fi = rep.fi;
		h->refs = 1;
		h->next = phx.handles;
		phx.handles = h;
		break;
	}

	case mtClose: {
		struct phx_handle **pp = &phx.handles, *h;
		while (*pp != NULL && (*pp)->ino != ino)
			pp = &(*pp)->next;
		if ((h = *pp) == NULL) { err = -EBADF; break; }
		if (--h->refs > 0)
			break;
		*pp = h->next;
		if (h->isdir)
			LL(releasedir, ino, &h->fi);
		else {
			LL(flush, ino, &h->fi);
			LL(release, ino, &h->fi);
		}
		free(h);
		break;
	}

	case mtRead:
	case mtWrite: {
		struct phx_handle *h = h_find(ino);
		if (h == NULL) { err = -EBADF; break; }
		dst = msg->o.data;
		dstlen = msg->o.size;
		if (msg->type == mtRead)
			LL(read, ino, msg->o.size, msg->i.io.offs, &h->fi);
		else
			LL(write, ino, msg->i.data, msg->i.size, msg->i.io.offs, &h->fi);
		if (err == 0)
			err = (int)rep.len;
		break;
	}

	case mtTruncate: {
		struct stat st = { .st_size = (off_t)msg->i.io.len };
		struct phx_handle *h = h_find(ino);
		LL(setattr, ino, &st, FUSE_SET_ATTR_SIZE, h ? &h->fi : NULL);
		break;
	}

	case mtSetAttr: {
		struct stat st;
		int set = 0;
		memset(&st, 0, sizeof(st));
		switch (msg->i.attr.type) {
		case atMode: st.st_mode = msg->i.attr.val; set = FUSE_SET_ATTR_MODE; break;
		case atUid: st.st_uid = msg->i.attr.val; set = FUSE_SET_ATTR_UID; break;
		case atGid: st.st_gid = msg->i.attr.val; set = FUSE_SET_ATTR_GID; break;
		case atSize: st.st_size = msg->i.attr.val; set = FUSE_SET_ATTR_SIZE; break;
		case atMTime: st.st_mtime = msg->i.attr.val; set = FUSE_SET_ATTR_MTIME; break;
		case atATime: st.st_atime = msg->i.attr.val; set = FUSE_SET_ATTR_ATIME; break;
		/* TODO atDev: record a mountpoint splice in a local table */
		default: err = -EINVAL; break;
		}
		if (set)
			LL(setattr, ino, &st, set, NULL);
		break;
	}

	case mtCreate: {
		const char *name = msg->i.data;
		mode_t mode = msg->i.create.mode & 07777;
		/* OpenBSD's fuse_ops.c relies on its kernel having LOOKUPed the
		 * name first (a failed lookup still allocates the vnode that
		 * mkdir/mknod then fill in). Phoenix mkdir sends mtCreate without a
		 * child lookup, so do it here: it also yields EEXIST. */
		LL(lookup, ino, name);
		if (err == 0) { err = -EEXIST; break; }
		err = 0;
		switch (msg->i.create.type) {
		case otDir: LL(mkdir, ino, name, mode); break;
		case otFile: LL(mknod, ino, name, S_IFREG | mode, 0); break;
		case otSymlink: {
			/* libphoenix symlink(): i.data = "name\0target\0" (inferred) */
			const char *target = name + strlen(name) + 1;
			LL(symlink, target, ino, name);
			break;
		}
		default: err = -EINVAL; break; /* otDev: local splice table, TODO */
		}
		if (err == 0) {
			msg->o.create.oid.port = port;
			msg->o.create.oid.id = rep.entry.ino;
		}
		break;
	}

	case mtUnlink: {
		const char *name = msg->i.data;
		LL(lookup, ino, name);
		if (err < 0)
			break;
		fuse_ino_t child = rep.entry.ino;
		int isdir = S_ISDIR(rep.entry.attr.st_mode);
		(void)child;
		if (isdir)
			LL(rmdir, ino, name);
		else
			LL(unlink, ino, name);
		break;
	}

	case mtLink:
		LL(link, msg->i.ln.oid.id, ino, msg->i.data);
		break;

	case mtRename: {
		const char *oldn = msg->i.data;
		const char *newn = oldn + strlen(oldn) + 1;
		/* same "kernel looked both names up first" contract as mtCreate */
		LL(lookup, ino, oldn);
		if (err < 0)
			break;
		LL(lookup, msg->i.ln.oid.id, newn);
		LL(rename, ino, oldn, msg->i.ln.oid.id, newn);
		break;
	}

	case mtReaddir: {
		/* Phoenix returns ONE entry per mtReaddir; the cookie is the
		 * cumulative d_namlen of preceding entries (dummyfs/nfs
		 * convention). Snapshot the whole listing once per scan. */
		struct phx_dirent *pd = msg->o.data;
		off_t offs = msg->i.readdir.offs, cum = 0;
		size_t i;

		if (offs == 0 || phx.snap.ino != ino) {
			struct fuse_file_info fi;
			char *buf = malloc(65536);
			off_t off = 0;

			snap_drop();
			phx.snap.ino = ino;
			memset(&fi, 0, sizeof(fi));
			LL(opendir, ino, &fi);
			if (err < 0) { free(buf); break; }
			fi = rep.fi;
			for (;;) {
				dst = buf; dstlen = 65536;
				LL(readdir, ino, 65536, off, &fi);
				if (err < 0 || rep.len == 0)
					break;
				size_t p = 0;
				struct fuse_dirent *d = NULL;
				while (p < rep.len) {
					d = (struct fuse_dirent *)(buf + p);
					phx.snap.ents = realloc(phx.snap.ents, (phx.snap.n + 1) * sizeof(struct phx_dent));
					phx.snap.ents[phx.snap.n].ino = d->ino;
					phx.snap.ents[phx.snap.n].type = dt2ot(d->type);
					/* fs often passes NULL stat for "."/".." */
					if ((d->namelen == 1 && d->name[0] == '.') ||
					    (d->namelen == 2 && d->name[0] == '.' && d->name[1] == '.'))
						phx.snap.ents[phx.snap.n].type = otDir;
					phx.snap.ents[phx.snap.n].name = strndup(d->name, d->namelen);
					phx.snap.n++;
					p += FUSE_DIRENT_SIZE(d);
				}
				if (d->off <= off)
					break;
				off = d->off;
			}
			dst = NULL; dstlen = 0;
			LL(releasedir, ino, &fi);
			free(buf);
			err = 0;
		}
		err = -ENOENT;
		for (i = 0; i < phx.snap.n; i++) {
			size_t nl = strlen(phx.snap.ents[i].name);
			if (cum == offs) {
				if (pd == NULL || msg->o.size < sizeof(*pd) + nl + 1) { err = -EINVAL; break; }
				pd->d_ino = phx.snap.ents[i].ino;
				pd->d_type = phx.snap.ents[i].type;
				pd->d_reclen = nl;
				pd->d_namlen = nl;
				memcpy(pd->d_name, phx.snap.ents[i].name, nl + 1);
				err = 0;
				break;
			}
			cum += nl;
		}
		break;
	}

	case mtStat: {
		LL(statfs, FUSE_ROOT_INO);
		if (err == 0) {
			if (msg->o.data == NULL || msg->o.size < sizeof(struct statvfs)) { err = -EINVAL; break; }
			memcpy(msg->o.data, &rep.stv, sizeof(struct statvfs));
		}
		break;
	}

	case 0xf52: {		/* mtSync (libphoenix sys/file.h), sent by fsync() */
		struct phx_handle *h = h_find(ino);
		if (h != NULL && !h->isdir)
			LL(fsync, ino, 0, &h->fi);
		break;
	}

	case mtDestroy:		/* FUSE has no equivalent; objects live in the fs */
		break;

	default:
		err = -EINVAL;
		break;
	}
	msg->o.err = err;
}

/* ---------------- the loop: Phoenix msgRecv/msgRespond ------------------ */

int fuse_session_loop(struct fuse_session *se)
{
	msg_t msg;
	int rid;

	phx.se = se;
	if (se->llops.init)
		se->llops.init(se->userdata, &se->fci);
	se->init = 1;

	while (!fuse_session_exited(se)) {
		if (phx_recv(se->chan, &msg, &rid) < 0)
			break;
		phx_dispatch(se, &msg);
		phx_respond(se->chan, &msg, rid);
	}
	return 0;
}
