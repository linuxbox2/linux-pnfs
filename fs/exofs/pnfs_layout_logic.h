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

#ifndef __splint__

#include "panfs_pnfs_ioctl.h"

#ifndef __LO_LIST_HEAD_DEFINED
struct lo_list_head {
	struct lo_list_head *next, *prev;
};
#define __LO_LIST_HEAD_DEFINED
#endif

#ifdef KERNEL

#if (__linux__ > 0)

#include <linux/wait.h>

static inline void fini_waitqueue_head(wait_queue_head_t *wq)
{
}

#else  /* NOT __linux__ */

#include <sys/condvar.h>
#include <sys/mutex.h>

struct wait_queue_head {
	struct mtx m;
	struct cv c;
};

typedef struct wait_queue_head wait_queue_head_t;

static inline void init_waitqueue_head(wait_queue_head_t *wq)
{
	mtx_init(&wq->m, __FILE__, NULL, MTX_DEF);
	cv_init(&wq->c, __FILE__);
}

#define wait_event_interruptible(wq, condition)			\
({								\
	int __ret = 0;						\
	PNFS_DBG(" waiting \n");				\
	mtx_lock(&(wq).m);					\
	while(!(condition)) {					\
		__ret = cv_wait_sig(&(wq).c, &(wq).m);		\
		if (__ret) {					\
			PNFS_DBG("breaking => %d\n", __ret);	\
			break;					\
		}						\
	}							\
	mtx_unlock(&(wq).m);					\
	PNFS_DBG("done waite\n");				\
	__ret;							\
})

static inline void wake_up(wait_queue_head_t *wq)
{
	mtx_lock(&wq->m);
	cv_signal(&wq->c);
	mtx_unlock(&wq->m);
}

static inline void fini_waitqueue_head(wait_queue_head_t *wq)
{
	cv_destroy(&wq->c);
	mtx_destroy(&wq->m);
}
#endif /* NOT __linux__ */
#endif /* KERNEL */

/* Usually this is only for regular files (that have layouts). In the case
   that this is not empty on the pannode->mountpoint->root_pannode, then
   layout_files is the list of file_pointer of the callback channels.

   In the root_pannode case layout_files is just the number of files opened
   for callback (Added when calling PAN_FS_CLIENT_PNFS_LAYOUTRECALL), cleaned
   on pck_pnfs_file_close. Thier layout-list is empty. The actuall recalls are
   on the recall_list instead.
 */
struct pnfs_node {
#ifdef KERNEL
	wait_queue_head_t wq;
#endif
	pan_spinlock_t layout_lock;

	/* These are layouts served */
	struct lo_list_head layouts; /* list of type pnfs_layout */
	void *recall_file_info;	     /* NFS Server supplied at layout_get */

	/* There can be only one on going layout_get per pannode */
	volatile enum pnfs_lo_get_state {
		E_LO_GET_NONE = 0,
		E_LO_GET_WAIT,
		E_LO_GET_SUCCESS,
		E_LO_GET_CANCLE,
	} layout_get_state;
	uint64_t last_layout_group_size;
	uint64_t last_layout_stripe_size;

	/* On the root_node these are for pnfs_lo_receive_recalls */
	/* On regular pnfs_node these hold the pending recalls */
	struct lo_list_head recalls;    /* list of type pnfs_recall */

	bool in_roc_state;
	bool recalls_canceled;
	void *pnfs_file;
	char sl_name[32];
};

struct pnfs_file {
	/* per file_pointer list, list of type pnfs_layout. A close of the
	 * file_pointer that GETed a layout will remove it from any list,
	 * pannode-list, for_recall, or in_recall
	 */
	struct lo_list_head per_file;

	/* my parent, fast access for locking */
/*	struct pnfs_node *pnfs_node;*/
};

struct pnfs_recall {
	/* Will be hang on root_node->recalls */
	struct lo_list_head recalls; /* list of type pnfs_recall */

	/* Each recall is also kept on a per node list for fast access */
	struct lo_list_head per_node; /* list of type pnfs_recall */

	/* These are the layouts that belong to this recall */
	struct lo_list_head layouts; /* list of type pnfs_layout */

	/* @ev.seg is the union of all segments attached to this recall */
	struct pan_cb_layoutrecall_event ev;
	void *waiter;
};

struct pnfs_layout {
	/* layout can be in one of two lists:
	 * 1. At data pannode after being served
	 * 2. At some pnfs_recall if is now part of a recall
	 */
	struct lo_list_head layouts; /* list of type pnfs_layout */

	/* On the openner's file_pointer list. A file CLOSE removes any layouts
	 * from any list
	 */
	struct lo_list_head per_file; /* list of type pnfs_layout */

	struct pnfs_segment seg;  	/* the served out layout info */
	uint64_t clientid;
	void *caps;
};

struct pnfs_layout *pnfs_lo_new(void);

int pnfs_pannode_init(pan_fs_client_cache_pannode_t *pannode);
/* Make sure all of nodes layouts are revoked before shutdown
 * This should ever come up empty, since all file_pointers have already
 * closed by now. It is just for safety
 */
void pnfs_pannode_release(pan_fs_client_cache_pannode_t *pannode);

static inline void _pnfs_node_lock(struct pnfs_node *pnfs_node)
{
	pan_spinlock_lock(&pnfs_node->layout_lock);
}

static inline void _pnfs_node_unlock(struct pnfs_node *pnfs_node)
{
	pan_spinlock_unlock(&pnfs_node->layout_lock);
}

void pnfs_file_init(struct pnfs_file* pnfs_file);
/* Remove any layouts assosiated with this file_pointer */
void pnfs_file_close(struct pnfs_file* pnfs_file,
			 pan_fs_client_cache_pannode_t *pannode);

/* return: 0=added, -ENOMEM, -EIO=backchannel-down-and-write */
int pnfs_lo_add2file(struct pnfs_layout* lo, struct pnfs_file* pnfs_file,
		    pan_fs_client_cache_pannode_t *pannode,
		    void *private_file_info);

/* release one cap */
void pnfs_lo_return(struct pnfs_layout* lo, struct pnfs_file* pnfs_file,
		   pan_fs_client_cache_pannode_t *pannode,
		   struct pnfs_recall *recall);

/* Release all pnfs layout caps on supper-block */
/* TODO: we don't keep a global list yet so it does *nothing*
 * Ganesha takes care of that, and if Ganesha crashes all file_pointer will
 * close and we'll clean just fine
 */
/*void pnfs_lo_return_all(pan_fs_client_cache_pannode_t *pannode);
*/

/* Add a recall to queue for all intersecting layouts in the range/iomode
 * return: 0=sent, -ENOENT=empty-match -EIO=back-channel-down
 */
int pnfs_lo_recall(pan_fs_client_cache_pannode_t *pannode, void *caps,
	uint64_t clientid, enum layoutiomode4 iomode,
	uint64_t offset, uint64_t length, void *waiter);

/* The PAN_FS_CLIENT_PNFS_LAYOUTRECALL ioctl will call this to deliver callbacks
 * to user-mode. It might deliver upto @max_events at the time.
 * If there are no events it will sleep until one arrives.
 * @pannode can be any node, recalls are always attched to root_pannode
 */
int pnfs_lo_receive_recalls(pan_fs_client_cache_pannode_t *pannode,
			struct pan_cb_layoutrecall_event *events,
			int max_events, bool allow_sleep);

int pnfs_lo_cancel_recalls(pan_fs_client_cache_pannode_t *pannode,
			  int debug_magic);
#else
static inline void pnfs_file_init(void *pnfs_file) {
}

static inline void pnfs_file_close(void *pnfs_file, void *pannode) {
}

static inline void pnfs_pannode_init(void *pannode) {
}

static inline void pnfs_pannode_release(void *pannode) {
}
#endif /* __splint__ */
#endif /* __PNFS_LAYOUT_LOGIC_H__ */
