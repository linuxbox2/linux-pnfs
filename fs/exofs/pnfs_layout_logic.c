/*
 * pnfs_layout_logic.c
 *
 * Description: pnfs layouts && recalls logic
 *
 * @author  bharrosh
 * @version 5.5
 *
 */


#include <linux/wait.h>

#include "exofs.h"
#include "lo_list.h"

#define LO_TRACE(fmt, a...) \
	printk(KERN_ERR "pnfs_lo @%s:%d: " fmt, __func__, __LINE__, ##a)

/* return a zero-out pkc_layout */
struct pkc_layout *pkc_lo_new(void)
{
	struct pkc_layout *lo = kzalloc(sizeof(*lo), GFP_KERNEL);

	if (lo) {
		lo_list_init(&lo->layouts);
		lo_list_init(&lo->per_file);
	}
	return lo;
}

void __lo_free(struct pkc_layout *lo)
{
	kfree(lo/*, sizeof(*lo)*/);
}

struct pkc_recall *__recall_new(void)
{
	struct pkc_recall *recall = kzalloc(sizeof(*recall), GFP_KERNEL);

	if (recall) {
		lo_list_init(&recall->recalls);
		lo_list_init(&recall->layouts);
		/* so min works in _recall_merge_seg */
		recall->ev.seg.offset = NFS4_MAX_UINT64;
	}

	return recall;
}

void __recall_free(struct pkc_recall *recall)
{
	kfree(recall/*, sizeof(*recall)*/);
}

void _pnfs_node_lock(struct pkc_pnfs_inode *pnfs_inode)
{
	spin_lock(&pnfs_inode->layout_lock);
}

void _pnfs_node_unlock(struct pkc_pnfs_inode *pnfs_inode)
{
	spin_unlock(&pnfs_inode->layout_lock);
}

// struct pkc_pnfs_inode *_pnfs_get_root(pan_fs_client_cache_pannode_t *pannode)
// {
// 	return &pannode->mountpoint->root_pannode->pnfs_inode;
// }

struct pkc_pnfs_inode *_pnfs_get_root(pan_fs_client_cache_pannode_t *pannode)
{
	return &exofs_i(pannode->vfs_inode.i_sb->s_root->d_inode)->pnfs_inode;
}

void _lo_caps_release(struct pkc_layout *lo)
{
// 	if (!lo->caps) {
// 		LO_TRACE("!!! release with no caps\n");
// 		return;
// 	}

/*	pan_fs_client_cap_release(lo->caps,
				  PAN_FS_CLIENT_CAP_DEPRECATE_F_NO_WAIT);*/
	lo->caps = NULL;
}

void _lo_detach(struct pkc_layout *lo)
{
	lo_list_del_init(&lo->layouts);
	lo_list_del_init(&lo->per_file);
}

void _lo_delete_list(struct lo_list_head *del_list)
{
	struct pkc_layout *lo, *t;

	if (lo_list_empty(del_list))
		return;

	lo_list_for_each_entry_safe(lo, t, del_list, layouts) {
		lo_list_del(&lo->layouts);
		_lo_caps_release(lo);
		__lo_free(lo);
	}
}

void pkc_pnfs_pannode_init(pan_fs_client_cache_pannode_t *pannode)
{
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;

LO_TRACE("pnfs_inode=%p\n", pnfs_inode);

	spin_lock_init(&pnfs_inode->layout_lock/*, __func__*/);
	lo_list_init(&pnfs_inode->layouts);
	lo_list_init(&pnfs_inode->for_recall);
	lo_list_init(&pnfs_inode->in_recall);
	init_waitqueue_head(&pnfs_inode->wq);

/* TODO: remove these are already zero */
	pnfs_inode->recall_file_info = NULL;
	pnfs_inode->recalls_canceled = false;
	pnfs_inode->pnfs_file = NULL;
}

/* Make sure all of nodes layouts are revoked before shutdown
 * This should ever come up empty, since all file_pointers have already
 * closed by now. It is just for safety
 */
void pkc_pnfs_pannode_release(pan_fs_client_cache_pannode_t *pannode)
{
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;
	struct lo_list_head del_list;
	struct pkc_layout* lo, *t;

// 	if (pannode->type != PAN_FS_CLIENT_CACHE_PANNODE_TYPE__FILE)
// 		return; /* optimize for dirs */

	lo_list_init(&del_list);

	_pnfs_node_lock(pnfs_inode);

	lo_list_for_each_entry_safe(lo, t, &pnfs_inode->layouts, layouts) {
		_lo_detach(lo);
		LO_TRACE("!!! inode has layouts(%p) on release\n", lo);
		lo_list_add(&lo->layouts, &del_list);
	}

	_pnfs_node_unlock(pnfs_inode);

	_lo_delete_list(&del_list);
LO_TRACE("pnfs_inode=%p\n", pnfs_inode);
}

void pkc_pnfs_file_init(struct pkc_pnfs_file* pnfs_file)
{
	lo_list_init(&pnfs_file->per_file);

LO_TRACE("pnfs_file=%p\n", pnfs_file);
}

void pkc_pnfs_file_close(struct pkc_pnfs_file* pnfs_file,
			 pan_fs_client_cache_pannode_t *pannode)
{
	struct pkc_pnfs_inode *pnfs_root = _pnfs_get_root(pannode);
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;
	struct lo_list_head del_list;
	struct pkc_layout* lo, *t;

// 	if (pannode->type != PAN_FS_CLIENT_CACHE_PANNODE_TYPE__FILE)
// 		return; /* optimize for dirs */
	if (S_ISDIR(pannode->vfs_inode.i_mode))
		return;

LO_TRACE("next=%p pnfs_root=%p pnfs_node=%p\n", pnfs_file->per_file.next, pnfs_root, pnfs_inode);
	lo_list_init(&del_list);

	_pnfs_node_lock(pnfs_root);
	_pnfs_node_lock(pnfs_inode);

	lo_list_for_each_entry_safe(lo, t, &pnfs_file->per_file, per_file) {
		_lo_detach(lo);
		lo_list_add(&lo->layouts, &del_list);
		LO_TRACE("!!! inode has layouts(%p) on file close\n", lo);
	}

	_pnfs_node_unlock(pnfs_inode);
	_pnfs_node_unlock(pnfs_root);

	_lo_delete_list(&del_list);
}

/* return: 0=added, -ENOMEM, -EIO=backchannel-down-and-write */
int pkc_lo_add2file(struct pkc_layout* lo, struct pkc_pnfs_file* pnfs_file,
		    pan_fs_client_cache_pannode_t *pannode,
		    void *recall_file_info)
{
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;

	_pnfs_node_lock(pnfs_inode);

	if (pnfs_inode->recall_file_info &&
	    pnfs_inode->recall_file_info != recall_file_info)
		LO_TRACE("recall_file_info(%p) != new_info(%p taking the last one\n",
			 pnfs_inode->recall_file_info, recall_file_info);
	pnfs_inode->recall_file_info = recall_file_info;

	lo_list_add(&lo->layouts, &pnfs_inode->layouts);
	lo_list_add(&lo->per_file, &pnfs_file->per_file);

	_pnfs_node_unlock(pnfs_inode);

	LO_TRACE("layout(%p) added on file(%p) recall_file_info=%p\n", lo, pnfs_file, recall_file_info);
	return 0;
}

/* release one cap */
void pkc_lo_return(struct pkc_layout* lo, struct pkc_pnfs_file* pnfs_file,
		   pan_fs_client_cache_pannode_t *pannode,
		   struct pkc_recall *recall)
{
	struct pkc_pnfs_inode *pnfs_root = _pnfs_get_root(pannode);
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;

	LO_TRACE("layout(%p) to RETURN\n", lo);

	_pnfs_node_lock(pnfs_root);
	_pnfs_node_lock(pnfs_inode);

	if (lo)
		_lo_detach(lo);

	if (recall) {
		if (lo_list_empty(&recall->layouts))
			lo_list_del(&recall->recalls);
		else
			/* If the recall cookie was given out of order, we still
			 * support it by not removing it at all, on the next
			 * receive_recalls, any empty pkc_recall(s) are deleted
			 */
			recall = NULL;
	}

	_pnfs_node_unlock(pnfs_inode);
	_pnfs_node_unlock(pnfs_root);

	if (lo) {
		_lo_caps_release(lo);
		__lo_free(lo);
	}
	if (recall)
		__recall_free(recall);

}

static uint64_t _last_offset(uint64_t offset, uint64_t length)
{
	if(length == NFS4_MAX_UINT64)
		return NFS4_MAX_UINT64;
	else
		/* It could overflow but never in our world it's just that
		 * len == NFS4_MAX_UINT64 special case that does come from
		 * code.
		 */
		return offset + length;
}
static uint64_t _seg_last_offset(struct pnfs_segment *seg)
{
	return _last_offset(seg->offset, seg->length);
}

static void _recall_merge_seg(struct pkc_recall *recall, struct pkc_layout *lo)
{
	uint64_t recall_last_offset = _seg_last_offset(&recall->ev.seg);
	uint64_t lo_last_offset = _seg_last_offset(&lo->seg);

	if (recall->ev.seg.offset > lo->seg.offset)
		recall->ev.seg.offset = lo->seg.offset;
	if (recall_last_offset < lo_last_offset)
		recall_last_offset = lo_last_offset;

	recall->ev.seg.length = (recall_last_offset == NFS4_MAX_UINT64) ?
				NFS4_MAX_UINT64 :
				recall_last_offset - recall->ev.seg.offset;

LO_TRACE("seg_off=%llu seg_len=%llu\n", recall->ev.seg.offset, recall->ev.seg.length);
}

bool seg_overlap(struct pnfs_segment *seg, uint64_t offset, uint64_t length)
{
	uint64_t seg_last_offset = _seg_last_offset(seg);
	uint64_t last_offset = _last_offset(offset, length);

LO_TRACE("seg_off=%llu seg_len=%llu off=%llu len=%llu\n", seg->offset, seg->length, offset, length);

	if (seg->offset <= offset && seg_last_offset > offset)
		return true;

	if (offset <= seg->offset && last_offset > seg->offset)
		return true;
	return false;
}

void __recalls_signal_event(struct pkc_pnfs_inode *pnfs_root)
{
/*LO_TRACE("IN\n");*/
	wake_up(&pnfs_root->wq);
/*LO_TRACE("OUT\n");*/
}

bool __recalls_done_wait(struct pkc_pnfs_inode *pnfs_root)
{
	return !lo_list_empty(&pnfs_root->for_recall) ||
	       pnfs_root->recalls_canceled;
}

int __recalls_wait_event(struct pkc_pnfs_inode *pnfs_root)
{
	int ret = wait_event_interruptible(pnfs_root->wq,
					   __recalls_done_wait(pnfs_root));
	return ret;
}

/* Add a recall to queue for all layouts in the range/iomode
 * return: 0=sent, -ENOMEM -ENOENT=empty-match -EIO=channel-down
 */
int pkc_lo_recall(pan_fs_client_cache_pannode_t *pannode,
		  enum layoutiomode4 iomode, uint64_t offset, uint64_t length,
		  void *waiter)
{
	struct pkc_pnfs_inode *pnfs_inode = &pannode->pnfs_inode;
	struct pkc_pnfs_inode *pnfs_root = _pnfs_get_root(pannode);
	struct pkc_recall *recall;
	struct pkc_layout *lo, *t;
	int ret = 0;

	recall = __recall_new();
	if (!recall)
		return -ENOMEM;

	_pnfs_node_lock(pnfs_inode);

	lo_list_for_each_entry_safe(lo, t, &pnfs_inode->layouts, layouts) {
		LO_TRACE("check lo(%p)\n", lo);
		if (seg_overlap(&lo->seg, offset, length)) {
			lo_list_del(&lo->layouts);
			lo_list_add(&lo->layouts, &recall->layouts);
			_recall_merge_seg(recall, lo);
		}
	}

	_pnfs_node_unlock(pnfs_inode);

	if (lo_list_empty(&recall->layouts)) {
		__recall_free(recall);

		ret = -ENOENT;
		_pnfs_node_lock(pnfs_root);
		/* Make sure these are not alraedy recalled */
		lo_list_for_each_entry(recall, &pnfs_root->for_recall, recalls) {
			if (seg_overlap(&recall->ev.seg, offset, length)) {
				if (lo_list_empty(&recall->layouts)) {
					LO_TRACE("!!! empty recall at ->for_recall\n");
				} else {
				/* There is a pending recall for this wait */
					LO_TRACE("Still waiting => -EAGAIN;\n");
					ret = -EAGAIN;
					break;
				}
			}
		}
		lo_list_for_each_entry(recall, &pnfs_root->in_recall, recalls) {
			if (seg_overlap(&recall->ev.seg, offset, length)) {
				if (lo_list_empty(&recall->layouts)) {
					LO_TRACE("!!! empty recall\n");
/*					lo_list_del(&recall->recalls);
					__recall_free(recall);*/
				} else {
				/* There is a pending recall for this wait */
					LO_TRACE("Still waiting => -EAGAIN;\n");
					ret = -EAGAIN;
					break;
				}
			}
		}
		_pnfs_node_unlock(pnfs_root);
	} else {
		recall->ev.recall_file_info = pnfs_inode->recall_file_info;
		recall->waiter = waiter;

		LO_TRACE("Adding recall(%p) recall_file_info=%p\n", recall, pnfs_inode->recall_file_info);
		_pnfs_node_lock(pnfs_root);

		/* TODO: check under_lock that backchannel fd still open */
		lo_list_add(&recall->recalls, &pnfs_root->for_recall);

		_pnfs_node_unlock(pnfs_root);

		__recalls_signal_event(pnfs_root);
		ret = 0;
	}

	return ret;
}

void _clean_empty_recalls(struct pkc_pnfs_inode *pnfs_root)
{
	struct pkc_recall *recall, *t;

LO_TRACE("pnfs_inode=%p\n", pnfs_root);

	lo_list_for_each_entry_safe(recall, t, &pnfs_root->in_recall, recalls) {
		if (lo_list_empty(&recall->layouts)) {
			lo_list_del(&recall->recalls);
			__recall_free(recall);
		}
	}
}

/* The PAN_FS_CLIENT_PNFS_LAYOUTRECALL ioctl will call this to deliver callbacks
 * to user-mode. It might deliver upto @max_events at the time.
 * If there are no events it will sleep until one arrives.
 * Return > 0 is number of events, < 0 is error code
 */
int pkc_lo_receive_recalls(pan_fs_client_cache_pannode_t *pannode,
			   struct pan_cb_layoutrecall_event *events,
			   int max_events, bool allow_sleep)
{
	struct pkc_pnfs_inode *pnfs_root = _pnfs_get_root(pannode);
	struct pkc_recall *recall, *t;
	bool dont_sleep = false;
	int e = 0;

again:
LO_TRACE("pnfs_inode=%p\n", pnfs_root);

	_pnfs_node_lock(pnfs_root);
	lo_list_for_each_entry_safe(recall, t, &pnfs_root->for_recall, recalls) {
		lo_list_del(&recall->recalls);
		lo_list_add(&recall->recalls, &pnfs_root->in_recall);
		events[e] = recall->ev;
		events[e].cookie = recall;
LO_TRACE("recall_file_info=%p cookie=%p\n", events[e].recall_file_info, recall);
		if (++e >= max_events)
			break;
	}

	if (pnfs_root->recalls_canceled) {
		pnfs_root->recalls_canceled = false;
		dont_sleep = true;
	}

	_pnfs_node_unlock(pnfs_root);

	if (!e && allow_sleep) {
		int rc;

		/* Garbage collect empty recalls while idle */
		_clean_empty_recalls(pnfs_root);

LO_TRACE("pnfs_inode=%p dont_sleep=%d\n", pnfs_root, dont_sleep);
		if (dont_sleep)
			return 0;

		rc = __recalls_wait_event(pnfs_root);
		if (rc) {
			LO_TRACE("boaz=>%d\n", rc);
			return rc; /* Interupted or channel down */
		}
		goto again;
	}

	LO_TRACE("=>%d\n", e);
	return e;
}

int pkc_lo_cancel_recalls(pan_fs_client_cache_pannode_t *pannode,
			  int debug_magic)
{
	struct pkc_pnfs_inode *pnfs_root;

	if (debug_magic) {
		if (0 == pkc_lo_recall(pannode, LAYOUTIOMODE4_ANY, 0, ~0LLU, NULL)) {
LO_TRACE("=>%d\n", 1);
			return 1;/* Some layouts where recalled */
		}
	}

	pnfs_root = _pnfs_get_root(pannode);
	_pnfs_node_lock(pnfs_root);
		pnfs_root->recalls_canceled = true;
	_pnfs_node_unlock(pnfs_root);

	__recalls_signal_event(pnfs_root);
LO_TRACE("=>%d\n", 0);
	return 0;
}
