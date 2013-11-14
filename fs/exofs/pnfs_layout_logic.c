/*
 * pnfs_layout_logic.c
 *
 * Description: pnfs layouts && recalls logic
 *
 * @author  bharrosh
 * @version 5.5
 *
 */

#ifndef __splint__

#include "exofs.h"
#include "lin_pnfs_types.h"
#include "pnfs_layout_logic.h"
#include "lo_list.h"

/* return a zero-out pnfs_layout */
struct pnfs_layout *pnfs_lo_new(void)
{
	struct pnfs_layout *lo = lo_zalloc(sizeof(*lo));

	if (lo) {
		lo_list_init(&lo->layouts);
		lo_list_init(&lo->per_file);
	}
	return lo;
}

static void __lo_free(struct pnfs_layout *lo)
{
	lo_free(lo/*, sizeof(*lo)*/);
}

static struct pnfs_recall *__recall_new(void)
{
	struct pnfs_recall *recall = lo_zalloc(sizeof(*recall));

	if (recall) {
		lo_list_init(&recall->recalls);
		lo_list_init(&recall->per_node);
		lo_list_init(&recall->layouts);
		/* so min works in _recall_merge_seg */
		recall->ev.seg.offset = NFS4_MAX_UINT64;
	}

	return recall;
}

static void __recall_free(struct pnfs_recall *recall)
{
	lo_free(recall/*, sizeof(*recall)*/);
}

static bool LO_IS_DIR(pan_fs_client_cache_pannode_t *pannode)
{
	return S_ISDIR(pannode->vfs_inode.i_mode);
}

static struct pnfs_node *_pnfs_get_root(pan_fs_client_cache_pannode_t *pannode)
{
// 	return pannode->mountpoint->root_pannode->pnfs_node;
	return exofs_i(pannode->vfs_inode.i_sb->s_root->d_inode)->pnfs_node;
}

static void _lo_caps_release(struct pnfs_layout *lo)
{
	if (!lo->caps) {
		PNFS_DBG("!!! release with no caps\n");
		return;
	}

	PNFS_DBG("caps=%p\n", lo->caps);
/*	pan_fs_client_cap_release(lo->caps,
				  PAN_FS_CLIENT_CAP_DEPRECATE_F_NO_WAIT);*/
	lo->caps = NULL;
}

static void _lo_detach(struct pnfs_layout *lo)
{
	lo_list_del_init(&lo->layouts);
	lo_list_del_init(&lo->per_file);
}

static void _lo_delete_list(struct lo_list_head *del_list)
{
	struct pnfs_layout *lo, *t;

	if (lo_list_empty(del_list))
		return;

	lo_list_for_each_entry_safe(lo, t, del_list, layouts) {
		lo_list_del(&lo->layouts);
		_lo_caps_release(lo);
		__lo_free(lo);
	}
}

int pnfs_pannode_init(pan_fs_client_cache_pannode_t *pannode)
{
	struct pnfs_node *pnfs_node;

// 	if(pannode->pnfs_node)
// 		pan_log(PAN_LOG_WARNING, PAN_LOG_NONE,
// 			"pnfs_pannode_init: Called with pannode->pnfs_node(%p) != NULL\n"
// 			"~S\n", pannode->pnfs_node);

	pnfs_node = lo_zalloc(sizeof(*pannode->pnfs_node));
	if (!pnfs_node) {
		PNFS_DBG("!!!! allocation failed\n");
		return -ENOMEM;
	}

PNFS_DBG("pnfs_node=%p\n", pnfs_node);

	pannode->pnfs_node = pnfs_node;

	sprintf(pnfs_node->sl_name, "pnsl%p", pnfs_node);
	pan_spinlock_init_name(&pnfs_node->layout_lock, pnfs_node->sl_name);
	lo_list_init(&pnfs_node->layouts);
	lo_list_init(&pnfs_node->recalls);
	init_waitqueue_head(&pnfs_node->wq);
	/* The rest are Zero, Yes?*/

	return 0;
}

/* Make sure all of nodes layouts are revoked before shutdown
 * This should ever come up empty, since all file_pointers have already
 * closed by now. It is just for safety
 */
void pnfs_pannode_release(pan_fs_client_cache_pannode_t *pannode)
{
	struct pnfs_node *pnfs_node = pannode->pnfs_node;
	struct lo_list_head del_list;
	struct pnfs_layout* lo, *t;

	if (!pnfs_node || LO_IS_DIR(pannode))
		return; /* optimize for dirs */

	lo_list_init(&del_list);

	_pnfs_node_lock(pnfs_node);
// 	pannode->redelegate_callback = NULL;

	lo_list_for_each_entry_safe(lo, t, &pnfs_node->layouts, layouts) {
		_lo_detach(lo);
		PNFS_DBG("!!! inode has layouts(%p) on release\n", lo);
		lo_list_add(&lo->layouts, &del_list);
	}

	_pnfs_node_unlock(pnfs_node);

	_lo_delete_list(&del_list);
	fini_waitqueue_head(&pnfs_node->wq);
	lo_free(pannode->pnfs_node);
	pannode->pnfs_node = NULL;
}

void pnfs_file_init(struct pnfs_file *pnfs_file)
{
	lo_list_init(&pnfs_file->per_file);
}

/* Both root and node locks held */
static void _lo_remove_empty_recalls(struct pnfs_node *pnfs_node)
{
	struct pnfs_recall *recall, *t;

	lo_list_for_each_entry_safe(recall, t, &pnfs_node->recalls, per_node) {
		if (lo_list_empty(&recall->layouts)) {
			lo_list_del(&recall->per_node);
			if (!lo_list_empty(&recall->recalls))
				lo_list_del(&recall->recalls);
			__recall_free(recall);
		}
	}
}

void pnfs_file_close(struct pnfs_file* pnfs_file,
			 pan_fs_client_cache_pannode_t *pannode)
{
	struct pnfs_node *pnfs_root = _pnfs_get_root(pannode);
	struct pnfs_node *pnfs_node = pannode->pnfs_node;
	struct lo_list_head del_list;
	struct pnfs_layout* lo, *t;

	if (!pnfs_node || LO_IS_DIR(pannode))
		return; /* optimize for dirs */

	lo_list_init(&del_list);

	_pnfs_node_lock(pnfs_node);
	_pnfs_node_lock(pnfs_root);

	lo_list_for_each_entry_safe(lo, t, &pnfs_file->per_file, per_file) {
		_lo_detach(lo);
		lo_list_add(&lo->layouts, &del_list);
		PNFS_DBG("!!! inode has layouts(%p) on file close\n", lo);
	}

	_lo_remove_empty_recalls(pnfs_node);
	_pnfs_node_unlock(pnfs_root);
	_pnfs_node_unlock(pnfs_node);

	_lo_delete_list(&del_list);
}

/* return: 0=added, -ENOMEM, -EIO=backchannel-down-and-write */
int pnfs_lo_add2file(struct pnfs_layout* lo, struct pnfs_file* pnfs_file,
		    pan_fs_client_cache_pannode_t *pannode,
		    void *recall_file_info)
{
	struct pnfs_node *pnfs_node = pannode->pnfs_node;

	_pnfs_node_lock(pnfs_node);

	if (pnfs_node->recall_file_info &&
	    pnfs_node->recall_file_info != recall_file_info)
		PNFS_DBG("recall_file_info(%p) != new_info(%p taking the last one\n",
			 pnfs_node->recall_file_info, recall_file_info);
	pnfs_node->recall_file_info = recall_file_info;

	lo_list_add(&lo->layouts, &pnfs_node->layouts);
	lo_list_add(&lo->per_file, &pnfs_file->per_file);

	_pnfs_node_unlock(pnfs_node);

	PNFS_DBG("layout(%p) added on file(%p) recall_file_info=%p\n", lo, pnfs_file, recall_file_info);
	return 0;
}

/* release one cap */
void pnfs_lo_return(struct pnfs_layout* lo, struct pnfs_file* pnfs_file,
		   pan_fs_client_cache_pannode_t *pannode,
		   struct pnfs_recall *recall)
{
	struct pnfs_node *pnfs_root = _pnfs_get_root(pannode);
	struct pnfs_node *pnfs_node = pannode->pnfs_node;

	if (!lo)
		return;


	_pnfs_node_lock(pnfs_node);
	_pnfs_node_lock(pnfs_root);

	pnfs_node->in_roc_state = true;

	_lo_detach(lo);

	/* We ignore the recall cookie all together and just check to
	*  see if above _lo_detach(lo) might have removed the last
	*  layout that belongs to a recall.
	*/
	_lo_remove_empty_recalls(pnfs_node);

	if (lo_list_empty(&pnfs_node->layouts) &&
	    lo_list_empty(&pnfs_node->recalls)) {
		pnfs_node->in_roc_state = false;
	}

	_pnfs_node_unlock(pnfs_root);
	_pnfs_node_unlock(pnfs_node);

	_lo_caps_release(lo);
	__lo_free(lo);

	PNFS_DBG("RETURN  layout(%p) pnfs_node=%p in_roc_state=%d\n",
		lo, pnfs_node, pnfs_node->in_roc_state);

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

static void _recall_merge_seg(struct pnfs_recall *recall, struct pnfs_layout *lo)
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

	if (!recall->ev.clientid) {
		recall->ev.clientid = lo->clientid;
	} else if (recall->ev.clientid != lo->clientid) {
		recall->ev.clientid = ~0UL;
		/* ~0 will never match and we look at the seg only.
		 * This will only happen if we originaly called with clientid=0
		 * case (See seg_conflict).
		 */
	}
	recall->ev.seg.io_mode |= lo->seg.io_mode;
}

/* seg_conflict:
 *
 * A simple tool to say if two segments overlap ie. cover the same area
 * and have the same IO MODE.
 * Here are the rules for "conflict"
 * [@seg.offset, @seg.legth] vs [@offset, @length] - Conflict if there is
   one or more bytes in the range that are the same.
 *
 * @seg.iomode vs @iomode - Conflict if they have the same mode or any one
 * of them is iomode ANY. (Note that @seg.iomode will never be ANY)
 *
 * @src_clientid vs @dest_clientid - Here the logic is oposite then above
 * two. Here Conflict is when dest_clientid is not NULL and that they do
 * not match. So a user of this tool can use a NULL client_id to always
 * match client IDs. Otherwise a Conflict is when two client_ids are different.
 *
 * Note: That this is just a simple utility that might match nonsensical cases
 * for example two READ segments. But policy is not governed here. The user of
 * pnfs_lo_recall() should set the policy. For example one should only call
 * pnfs_lo_recall() with IOMODE ANY or RW, and so on.
 */
static bool seg_conflict(struct pnfs_segment *seg, enum layoutiomode4 iomode,
			uint64_t offset, uint64_t length,
			uint64_t src_clientid, uint64_t dest_clientid)
{
	uint64_t seg_last_offset;
	uint64_t last_offset;

	/* dest_clientid==NULL means we are recalling ALL (MAPs changed) */
	/* dest_clientid!=NULL only recall from not us */
	if (dest_clientid && dest_clientid == src_clientid)
		return false;

	/* If READ & WRITE || WRITE & READ then no conflict */
	if (!(seg->io_mode & iomode))
		return false;

	seg_last_offset = _seg_last_offset(seg);
	last_offset = _last_offset(offset, length);
	if (seg->offset <= offset && seg_last_offset > offset)
		return true;

	if (offset <= seg->offset && last_offset > seg->offset)
		return true;
	return false;
}

static void __recalls_signal_event(struct pnfs_node *pnfs_root)
{
	wake_up(&pnfs_root->wq);
}

static bool __recalls_done_wait(struct pnfs_node *pnfs_root)
{
	return !lo_list_empty(&pnfs_root->recalls) ||
	       pnfs_root->recalls_canceled;
}

static int __recalls_wait_event(struct pnfs_node *pnfs_root)
{
	int ret = 0;

	ret = wait_event_interruptible(pnfs_root->wq,
				__recalls_done_wait(pnfs_root));
	return ret;
}

/* Add a recall to queue for all layouts in the range/iomode
 * return: 0=sent, -ENOMEM, -ENOENT=empty-match, -EAGAIN=in_recall
 *         -EIO=channel-down
 */
int pnfs_lo_recall(pan_fs_client_cache_pannode_t *pannode, void *caps,
	uint64_t clientid, enum layoutiomode4 iomode,
	uint64_t offset, uint64_t length, void *waiter)
{
	struct pnfs_node *pnfs_node = pannode->pnfs_node;
	struct pnfs_recall *recall;
	struct pnfs_layout *lo, *t;
	int ret;

	recall = __recall_new();
	if (!recall)
		return -ENOMEM;

	_pnfs_node_lock(pnfs_node);

	lo_list_for_each_entry_safe(lo, t, &pnfs_node->layouts, layouts) {
		if (seg_conflict(&lo->seg, iomode, offset, length,
						lo->clientid, clientid)) {
			if (!caps || (caps == lo->caps)){
				lo_list_del(&lo->layouts);
				lo_list_add(&lo->layouts, &recall->layouts);
				_recall_merge_seg(recall, lo);
			} else {
				PNFS_DBG("seg_conflict but caps(%p) != lo->caps(%p)\n",
					caps, lo->caps);
			}
		}
	}

	if (lo_list_empty(&recall->layouts)) {
		ret = -ENOENT;
		__recall_free(recall);
		/* Make sure these are not alraedy recalled */
		lo_list_for_each_entry(recall, &pnfs_node->recalls, per_node) {
			if (seg_conflict(&recall->ev.seg, iomode, offset, length,
					 recall->ev.clientid, clientid)) {
				if (lo_list_empty(&recall->layouts)) {
					/* Should never happen */
					PNFS_DBG("!!! empty recall\n");
				} else {
				/* There is a pending recall for this wait */
					lo_list_for_each_entry(lo, &recall->layouts, layouts) {
						_lo_caps_release(lo);
					}
					ret = -EAGAIN;
				}
			}
		}
		recall = NULL;
	} else {
		struct pnfs_node *pnfs_root = _pnfs_get_root(pannode);

		recall->ev.recall_file_info = pnfs_node->recall_file_info;
		recall->waiter = waiter;

		lo_list_add(&recall->per_node, &pnfs_node->recalls);

/*		if (pnfs_node->in_roc_state) {
			lo_list_for_each_entry(lo, &recall->layouts, layouts) {
				_lo_caps_release(lo);
			}
		} else {*/
			_pnfs_node_lock(pnfs_root);
			lo_list_add(&recall->recalls, &pnfs_root->recalls);
			_pnfs_node_unlock(pnfs_root);

			__recalls_signal_event(pnfs_root);
// 		}

		ret = 0;
	}

	_pnfs_node_unlock(pnfs_node);
	PNFS_DBG("recall(%p) CAPs(%p) in_roc_state=%d => %d\n",
		recall, caps, pnfs_node->in_roc_state, ret);

	return ret;
}

/* The PAN_FS_CLIENT_PNFS_LAYOUTRECALL ioctl will call this to deliver callbacks
 * to user-mode. It might deliver upto @max_events at the time.
 * If there are no events it will sleep until one arrives.
 * Return > 0 is number of events, < 0 is error code
 */
int pnfs_lo_receive_recalls(pan_fs_client_cache_pannode_t *pannode,
			   struct pan_cb_layoutrecall_event *events,
			   int max_events, bool allow_sleep)
{
	struct pnfs_node *pnfs_root = _pnfs_get_root(pannode);
	struct pnfs_recall *recall, *t;
	bool dont_sleep = false;
	int e = 0;

again:
	_pnfs_node_lock(pnfs_root);

	lo_list_for_each_entry_safe(recall, t, &pnfs_root->recalls, recalls) {
		lo_list_del_init(&recall->recalls);
		events[e] = recall->ev;
		events[e].cookie = recall;
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

PNFS_DBG("pnfs_node=%p dont_sleep=%d\n", pnfs_root, dont_sleep);
		if (dont_sleep)
			return 0;

		rc = __recalls_wait_event(pnfs_root);
		if (rc) {
			PNFS_DBG("=>%d\n", rc);
			return rc; /* Interupted or channel down */
		}
		goto again;
	}

	PNFS_DBG("=>%d\n", e);
	return e;
}

int pnfs_lo_cancel_recalls(pan_fs_client_cache_pannode_t *pannode,
			  int debug_magic)
{
	struct pnfs_node *pnfs_root;

	if (debug_magic) {
		if (0 == pnfs_lo_recall(pannode, NULL, 0,
		                        LAYOUTIOMODE4_ANY, 0, ~0LLU, NULL)) {
			PNFS_DBG("debug_magic: Some layouts where recalled\n");
			return 1;/* Some layouts where recalled */
		}
	}

	pnfs_root = _pnfs_get_root(pannode);
	_pnfs_node_lock(pnfs_root);
		pnfs_root->recalls_canceled = true;
	_pnfs_node_unlock(pnfs_root);

	__recalls_signal_event(pnfs_root);
PNFS_DBG("=>%d\n", 0);
	return 0;
}
#endif /* __splint__ */
