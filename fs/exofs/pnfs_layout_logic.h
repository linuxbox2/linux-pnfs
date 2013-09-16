/*
 * pnfs_layout_logic.h
 *
 * Description: pnfs layouts && recalls logic
 *
 * @author  bharrosh
 * @version 5.5
 *
 */

#ifndef __PNFS_LAYOUT_LOGIC_H__
#define __PNFS_LAYOUT_LOGIC_H__

#include "panfs_pnfs_ioctl.h"
#include <linux/wait.h>

#ifndef __LO_LIST_HEAD_DEFINED
struct lo_list_head {
	struct lo_list_head *next, *prev;
};
#define __LO_LIST_HEAD_DEFINED
#endif

typedef struct exofs_i_info pan_fs_client_cache_pannode_t;

/* Usually this is only for regular files (that have layouts). In the case
   that this is not empty on the pannode->mountpoint->root_pannode, then
   layout_files is the list of file_pointer of the callback channels.

   In the root_pannode case layout_files is just the number of files opened
   for callback (Added when calling PAN_FS_CLIENT_PNFS_LAYOUTRECALL), cleaned
   on pck_pnfs_file_close. Thier layout-list is empty. The actuall recalls are
   on the recall_list instead.
 */

struct pkc_pnfs_inode {
	spinlock_t layout_lock;
	/* These are layouts served */
	struct lo_list_head layouts; /* list of type pkc_layout */
	void *recall_file_info;	     /* NFS Server supplied at layout_get */

	/* Only used by root_pannode */
	struct lo_list_head for_recall;    /* list of type pkc_recall */
	struct lo_list_head in_recall;  /* list of type pkc_recall */
	bool recalls_canceled;
	void *pnfs_file;

#ifdef KERNEL
	wait_queue_head_t wq;
#endif
};

struct pkc_pnfs_file {
	/* per file_pointer list, list of type pkc_layout. A close of the
	 * file_pointer that GETed a layout will remove it from any list,
	 * pannode-list, for_recall, or in_recall
	 */
	struct lo_list_head per_file;

	/* my parent, fast access for locking */
/*	struct pkc_pnfs_inode *pnfs_inode;*/
};

struct pkc_recall {
	/* Will be hang on either for_recall or in_recall lists at pnfs_inode */
	struct lo_list_head recalls; /* list of type pkc_recall */

	/* These are the layouts that belong to this recall */
	struct lo_list_head layouts; /* list of type pkc_layout */

	/* @ev.seg is the union of all segments attached to this recall */
	struct pan_cb_layoutrecall_event ev;
	void *waiter;
};

struct pkc_layout {
	/* layout can be in one of two lists:
	 * 1. At data pannode after being served
	 * 2. At some pkc_recall if is now part of a recall
	 */
	struct lo_list_head layouts; /* list of type pkc_layout */

	/* On the openner's file_pointer list. A file CLOSE removes any layouts
	 * from any list
	 */
	struct lo_list_head per_file; /* list of type pkc_layout */

	struct pnfs_segment seg;  	/* the served out layout info */
	uint64_t clientid;
	void *caps;
};

struct pkc_layout *pkc_lo_new(void);

void pkc_pnfs_pannode_init(pan_fs_client_cache_pannode_t *pannode);
/* Make sure all of nodes layouts are revoked before shutdown
 * This should ever come up empty, since all file_pointers have already
 * closed by now. It is just for safety
 */
void pkc_pnfs_pannode_release(pan_fs_client_cache_pannode_t *pannode);

void pkc_pnfs_file_init(struct pkc_pnfs_file* pnfs_file);
/* Remove any layouts assosiated with this file_pointer */
void pkc_pnfs_file_close(struct pkc_pnfs_file* pnfs_file,
			 pan_fs_client_cache_pannode_t *pannode);

/* return: 0=added, -ENOMEM, -EIO=backchannel-down-and-write */
int pkc_lo_add2file(struct pkc_layout* lo, struct pkc_pnfs_file* pnfs_file,
		    pan_fs_client_cache_pannode_t *pannode,
		    void *private_file_info);

/* release one cap */
void pkc_lo_return(struct pkc_layout* lo, struct pkc_pnfs_file* pnfs_file,
		   pan_fs_client_cache_pannode_t *pannode,
		   struct pkc_recall *recall);

/* Release all pnfs layout caps on supper-block */
/* TODO: we don't keep a global list yet so it does *nothing*
 * Ganesha takes care of that, and if Ganesha crashes all file_pointer will
 * close and we'll clean just fine
 */
/*void pkc_lo_return_all(pan_fs_client_cache_pannode_t *pannode);
*/

/* Add a recall to queue for all intersecting layouts in the range/iomode
 * return: 0=sent, -ENOENT=empty-match -EIO=back-channel-down
 */
int pkc_lo_recall(pan_fs_client_cache_pannode_t *pannode,
		  enum layoutiomode4 iomode, uint64_t offset, uint64_t length,
		  void *waiter);

/* The PAN_FS_CLIENT_PNFS_LAYOUTRECALL ioctl will call this to deliver callbacks
 * to user-mode. It might deliver upto @max_events at the time.
 * If there are no events it will sleep until one arrives.
 * @pannode can be any node, recalls are always attched to root_pannode
 */
int pkc_lo_receive_recalls(pan_fs_client_cache_pannode_t *pannode,
			struct pan_cb_layoutrecall_event *events,
			int max_events, bool allow_sleep);

int pkc_lo_cancel_recalls(pan_fs_client_cache_pannode_t *pannode,
			  int debug_magic);

int pan_fs_client_pnfs_ioctl(
	pan_fs_client_cache_pannode_t		*pannode,
	struct pkc_pnfs_file 			*pnfs_file,
	uint32_t				command,
	void					*data);

#endif /* __PNFS_LAYOUT_LOGIC_H__ */
