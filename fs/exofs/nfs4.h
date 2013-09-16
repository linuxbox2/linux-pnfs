/*
 * src/panfs/include/pan/fs/client/unix/nfs4.h
 *
 * This file is included from ./fsal_pnfs.h (see comments there) which is
 * A Ganesha project file, which defines the types communicated with the
 * Ganesha user-mode server.
 * In Ganesha, there is a very large file nfs4.h which defines a few types
 * needed by fsal_pnfs.h.
 * Instead of taking the complete ganesha/.../nfs4.h which is conflicting
 * with linux/nfs4.h, we just define those few types needed.
 *
 * If in user-mode test application or on FreeBSD we also define here
 * the few types used which are defined in linux/nfs4.h, else if in linux kernel
 * we #include <linux/nfs4.h>
 *
 * @author bharrosh
 * @version 5.5
 *
 */
/*
 * This file is not copyrighted since it borrows uncopyrightable types from
 * other headers
 */

#ifndef __PKC_NFS4_H__
#define __PKC_NFS4_H__

enum layoutiomode4 {
	LAYOUTIOMODE4_READ = 1,
	LAYOUTIOMODE4_RW = 2,
	LAYOUTIOMODE4_ANY = 3,
};
typedef enum layoutiomode4 layoutiomode4;

enum layouttype4 {
	LAYOUT4_NFSV4_1_FILES = 0x1,
	LAYOUT4_OSD2_OBJECTS = 0x2,
	LAYOUT4_BLOCK_VOLUME = 0x3,
};
typedef enum layouttype4 layouttype4;

enum layoutreturn_type4 {
	LAYOUTRETURN4_FILE = 1,
	LAYOUTRETURN4_FSID = 2,
	LAYOUTRETURN4_ALL = 3,
};
typedef enum layoutreturn_type4 layoutreturn_type4;

#if (KERNEL == 0) || (__linux__ == 0)
enum nfsstat4 {
	NFS4_OK = 0,
	NFS4ERR_PERM = 1,
	NFS4ERR_NOENT = 2,
	NFS4ERR_IO = 5,
	NFS4ERR_NXIO = 6,
	NFS4ERR_ACCESS = 13,
	NFS4ERR_EXIST = 17,
	NFS4ERR_XDEV = 18,
	/* Unused/reserved 19 */
	NFS4ERR_NOTDIR = 20,
	NFS4ERR_ISDIR = 21,
	NFS4ERR_INVAL = 22,
	NFS4ERR_FBIG = 27,
	NFS4ERR_NOSPC = 28,
	NFS4ERR_ROFS = 30,
	NFS4ERR_MLINK = 31,
	NFS4ERR_NAMETOOLONG = 63,
	NFS4ERR_NOTEMPTY = 66,
	NFS4ERR_DQUOT = 69,
	NFS4ERR_STALE = 70,
	NFS4ERR_BADHANDLE = 10001,
	NFS4ERR_BAD_COOKIE = 10003,
	NFS4ERR_NOTSUPP = 10004,
	NFS4ERR_TOOSMALL = 10005,
	NFS4ERR_SERVERFAULT = 10006,
	NFS4ERR_BADTYPE = 10007,
	NFS4ERR_DELAY = 10008,
	NFS4ERR_SAME = 10009,
	NFS4ERR_DENIED = 10010,
	NFS4ERR_EXPIRED = 10011,
	NFS4ERR_LOCKED = 10012,
	NFS4ERR_GRACE = 10013,
	NFS4ERR_FHEXPIRED = 10014,
	NFS4ERR_SHARE_DENIED = 10015,
	NFS4ERR_WRONGSEC = 10016,
	NFS4ERR_CLID_INUSE = 10017,
	NFS4ERR_RESOURCE = 10018,
	NFS4ERR_MOVED = 10019,
	NFS4ERR_NOFILEHANDLE = 10020,
	NFS4ERR_MINOR_VERS_MISMATCH = 10021,
	NFS4ERR_STALE_CLIENTID = 10022,
	NFS4ERR_STALE_STATEID = 10023,
	NFS4ERR_OLD_STATEID = 10024,
	NFS4ERR_BAD_STATEID = 10025,
	NFS4ERR_BAD_SEQID = 10026,
	NFS4ERR_NOT_SAME = 10027,
	NFS4ERR_LOCK_RANGE = 10028,
	NFS4ERR_SYMLINK = 10029,
	NFS4ERR_RESTOREFH = 10030,
	NFS4ERR_LEASE_MOVED = 10031,
	NFS4ERR_ATTRNOTSUPP = 10032,
	NFS4ERR_NO_GRACE = 10033,
	NFS4ERR_RECLAIM_BAD = 10034,
	NFS4ERR_RECLAIM_CONFLICT = 10035,
	NFS4ERR_BADXDR = 10036,
	NFS4ERR_LOCKS_HELD = 10037,
	NFS4ERR_OPENMODE = 10038,
	NFS4ERR_BADOWNER = 10039,
	NFS4ERR_BADCHAR = 10040,
	NFS4ERR_BADNAME = 10041,
	NFS4ERR_BAD_RANGE = 10042,
	NFS4ERR_LOCK_NOTSUPP = 10043,
	NFS4ERR_OP_ILLEGAL = 10044,
	NFS4ERR_DEADLOCK = 10045,
	NFS4ERR_FILE_OPEN = 10046,
	NFS4ERR_ADMIN_REVOKED = 10047,
	NFS4ERR_CB_PATH_DOWN = 10048,

	/* nfs41 */
	NFS4ERR_BADIOMODE	= 10049,
	NFS4ERR_BADLAYOUT	= 10050,
	NFS4ERR_BAD_SESSION_DIGEST = 10051,
	NFS4ERR_BADSESSION	= 10052,
	NFS4ERR_BADSLOT		= 10053,
	NFS4ERR_COMPLETE_ALREADY = 10054,
	NFS4ERR_CONN_NOT_BOUND_TO_SESSION = 10055,
	NFS4ERR_DELEG_ALREADY_WANTED = 10056,
	NFS4ERR_BACK_CHAN_BUSY	= 10057,	/* backchan reqs outstanding */
	NFS4ERR_LAYOUTTRYLATER	= 10058,
	NFS4ERR_LAYOUTUNAVAILABLE = 10059,
	NFS4ERR_NOMATCHING_LAYOUT = 10060,
	NFS4ERR_RECALLCONFLICT	= 10061,
	NFS4ERR_UNKNOWN_LAYOUTTYPE = 10062,
	NFS4ERR_SEQ_MISORDERED = 10063, 	/* unexpected seq.id in req */
	NFS4ERR_SEQUENCE_POS	= 10064,	/* [CB_]SEQ. op not 1st op */
	NFS4ERR_REQ_TOO_BIG	= 10065,	/* request too big */
	NFS4ERR_REP_TOO_BIG	= 10066,	/* reply too big */
	NFS4ERR_REP_TOO_BIG_TO_CACHE = 10067,	/* rep. not all cached */
	NFS4ERR_RETRY_UNCACHED_REP = 10068,	/* retry & rep. uncached */
	NFS4ERR_UNSAFE_COMPOUND = 10069,	/* retry/recovery too hard */
	NFS4ERR_TOO_MANY_OPS	= 10070,	/* too many ops in [CB_]COMP */
	NFS4ERR_OP_NOT_IN_SESSION = 10071,	/* op needs [CB_]SEQ. op */
	NFS4ERR_HASH_ALG_UNSUPP = 10072,	/* hash alg. not supp. */
						/* Error 10073 is unused. */
	NFS4ERR_CLIENTID_BUSY	= 10074,	/* clientid has state */
	NFS4ERR_PNFS_IO_HOLE	= 10075,	/* IO to _SPARSE file hole */
	NFS4ERR_SEQ_FALSE_RETRY	= 10076,	/* retry not original */
	NFS4ERR_BAD_HIGH_SLOT	= 10077,	/* sequence arg bad */
	NFS4ERR_DEADSESSION	= 10078,	/* persistent session dead */
	NFS4ERR_ENCR_ALG_UNSUPP = 10079,	/* SSV alg mismatch */
	NFS4ERR_PNFS_NO_LAYOUT	= 10080,	/* direct I/O with no layout */
	NFS4ERR_NOT_ONLY_OP	= 10081,	/* bad compound */
	NFS4ERR_WRONG_CRED	= 10082,	/* permissions:state change */
	NFS4ERR_WRONG_TYPE	= 10083,	/* current operation mismatch */
	NFS4ERR_DIRDELEG_UNAVAIL = 10084,	/* no directory delegation */
	NFS4ERR_REJECT_DELEG	= 10085,	/* on callback */
	NFS4ERR_RETURNCONFLICT	= 10086,	/* outstanding layoutreturn */
	NFS4ERR_DELEG_REVOKED	= 10087,	/* deleg./layout revoked */
};

typedef enum nfsstat4 nfsstat4;

struct nfstime4 {
	int64_t seconds;
	uint32_t nseconds;
};
typedef struct nfstime4 nfstime4;

/*NOTE: This is an curses.h bug that wants NCURSES_ENABLE_STDBOOL_H
 * defined in order to be combatble with stdbool.h. So in case curses.h did its
 * dead, revert it and do the proper thing.
 */
#undef bool
typedef _Bool bool;
#else
#include <linux/nfs4.h>
struct fsal_nfstime4 {
	int64_t seconds;
	uint32_t nseconds;
};
typedef struct fsal_nfstime4 nfstime4;
#endif /* not Linux Kernel */

#endif /* ifndef __PKC_NFS4_H__ */
