/*
 * export.c - Implementation of the pnfs_export_operations
 *
 * Copyright (C) 2009 Panasas Inc.
 * All rights reserved.
 *
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "exofs.h"
#include "pnfs_osd_xdr_srv.h"

#include <linux/completion.h>

/* TODO: put in sysfs per sb */
const static unsigned sb_shared_num_stripes = 8;

// static int exofs_layout_type(struct super_block *sb)
// {
// 	return LAYOUT_OSD2_OBJECTS;
// }

static void set_dev_id(struct nfs4_deviceid *pnfs_devid, u64 sbid, u64 devid)
{
	struct nfsd4_pnfs_deviceid *dev_id =
		(struct nfsd4_pnfs_deviceid *)pnfs_devid;

	dev_id->sbid  = sbid;
	dev_id->devid = devid;
}

static int cb_layout_recall(
	pan_fs_client_cache_pannode_t *pannode,
	enum layoutiomode4 io_mode,
	u64 offset, u64 length, void *waiter)
{
	int err = pnfs_lo_recall(pannode, NULL, 0, io_mode, offset, length, waiter);
	return err;
}

void ore_layout_2_pnfs_layout(struct pnfs_osd_layout *pl,
			      const struct ore_layout *ol)
{
	pl->olo_map.odm_stripe_unit = ol->stripe_unit;
	pl->olo_map.odm_mirror_cnt = ol->mirrors_p1 - 1;
	pl->olo_map.odm_raid_algorithm = ol->raid_algorithm;
	if (ol->group_count > 1) {
		pl->olo_map.odm_num_comps = ol->group_width * ol->mirrors_p1 *
				    ol->group_count;
		pl->olo_map.odm_group_width = ol->group_width;
		pl->olo_map.odm_group_depth = ol->group_depth;
	} else {
		/* If we don't do this here group_depth will not be correct
		 * because it is 32 bit only in pNFS
		 */
		pl->olo_map.odm_num_comps = ol->group_width * ol->mirrors_p1;
		pl->olo_map.odm_group_width = 0;
		pl->olo_map.odm_group_depth = 0;
	}
}

static bool _align_io(struct ore_layout *layout, struct pnfs_segment *lseg,
		      bool shared)
{
	u64 stripe_size = (layout->group_width - layout->parity) *
							layout->stripe_unit;
	u64 group_size = stripe_size * layout->group_depth;

	/* TODO: Don't ignore shared flag. Single writer can get a full group */
	if (lseg->io_mode != LAYOUTIOMODE4_READ &&
	    (layout->parity || (layout->mirrors_p1 > 1))) {
		/* RAID writes */
		lseg->offset = div64_u64(lseg->offset, stripe_size) *
								stripe_size;
		lseg->length = stripe_size * sb_shared_num_stripes;
		return true;
	} else {
		/* reads or no data redundancy */
		lseg->offset = div64_u64(lseg->offset, group_size) * group_size;
		lseg->length = group_size;
		return false;
	}
}

static enum nfsstat4
_pnfs_layout_get(
	pan_fs_client_cache_pannode_t *oi,
	struct pnfs_file *pnfs_file,
	struct exp_xdr_stream *xdr,
	uint64_t clientid,
	void *recall_file_info,
	const struct fsal_layoutget_arg *args,
	struct fsal_layoutget_res *res)
{
	struct inode *inode = &oi->vfs_inode;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct ore_striping_info si;
	struct pnfs_osd_layout layout;
	struct pnfs_layout* pnfs_layout;
	__be32 *start;
	unsigned i;
	bool need_recall;
	enum nfsstat4 nfserr;

	EXOFS_DBGMSG("(0x%lx) REQUESTED offset=0x%llx len=0x%llx iomod=0x%x\n",
		     inode->i_ino, res->segment.offset,
		     res->segment.length, res->segment.io_mode);

	need_recall = _align_io(&sbi->layout, &res->segment,
				1 /*see if !is_empty(lo_list) */ );

	EXOFS_DBGMSG("(0x%lx) RETURNED offset=0x%llx len=0x%llx iomod=0x%x\n",
		     inode->i_ino, res->segment.offset,
		     res->segment.length, res->segment.io_mode);

	if (need_recall) {
		int rc;
/* SIMULATE DF Map grow */
		u64 recall_offset, recall_len;
		u64 i_size = i_size_read(inode);
		u64 lo_end = res->segment.offset + res->segment.length;

		if (lo_end > i_size) {
			EXOFS_DBGMSG("(0x%lx) @@@ simulate map groth lo_end(0x%llx) > i_size(0x%llx)\n",
				     inode->i_ino, lo_end, i_size);
			recall_offset = 0;
			recall_len = NFS4_MAX_UINT64;
		} else {
			recall_offset = res->segment.offset;
			recall_len = res->segment.length;
		}
/* SIMULATE DF Map grow */

		rc = cb_layout_recall(oi, LAYOUTIOMODE4_RW,
				      recall_offset, recall_len , NULL);
		switch (rc) {
		case 0:
			EXOFS_DBGMSG("(0x%lx) @@@ Sharing of RAID5/1 stripe => NFS4ERR_RECALLCONFLICT\n",
				     inode->i_ino);
			return NFS4ERR_RECALLCONFLICT;
		case -EAGAIN:
			EXOFS_DBGMSG("(0x%lx) @@@ Not ready wait => NFS4ERR_RECALLCONFLICT\n",
				     inode->i_ino);
			return NFS4ERR_RECALLCONFLICT;
		default:
			/* This is fine for now */
			/* TODO: Fence object off */
			EXOFS_DBGMSG("(0x%lx) !!!cb_layout_recall => %d\n",
				     inode->i_ino, rc);
			/*fallthrough*/
		case -ENOENT:
			break;
		}
	}

	pnfs_layout = pnfs_lo_new();
	if (unlikely(!pnfs_layout))
		return NFS4ERR_LAYOUTTRYLATER; /*should this be NFS4ERR_RESOURCE*/

	start = xdr->p;

	/* Fill in a pnfs_osd_layout struct */
	ore_layout_2_pnfs_layout(&layout, &sbi->layout);

	ore_calc_stripe_info(&sbi->layout, res->segment.offset, 0, &si);
	layout.olo_comps_index = si.dev - (si.dev %
			(sbi->layout.group_width * sbi->layout.mirrors_p1));
	layout.olo_num_comps = sbi->layout.group_width * sbi->layout.mirrors_p1;

	nfserr = pnfs_osd_xdr_encode_layout_hdr(xdr, &layout);
	if (unlikely(nfserr))
		goto out;

	/* Encode layout components */
	for (i = 0; i < layout.olo_num_comps; i++) {
		struct pnfs_osd_object_cred cred;
		struct exofs_dev *ed = container_of(oi->oc.ods[i],
							typeof(*ed), ored);

		EXOFS_DBGMSG("   (0x%lx) i=%u export_id=0x%llx did=0x%x\n",
				inode->i_ino, i, args->export_id, ed->did);
		set_dev_id(&cred.oc_object_id.oid_device_id, args->export_id,
			   ed->did);
		cred.oc_object_id.oid_partition_id = oi->one_comp.obj.partition;
		cred.oc_object_id.oid_object_id = oi->one_comp.obj.id;
		cred.oc_osd_version = osd_dev_is_ver1(ed->ored.od) ?
						PNFS_OSD_VERSION_1 :
						PNFS_OSD_VERSION_2;
		cred.oc_cap_key_sec = PNFS_OSD_CAP_KEY_SEC_NONE;

		cred.oc_cap_key.cred_len	= 0;
		cred.oc_cap_key.cred		= NULL;

		cred.oc_cap.cred_len	= OSD_CAP_LEN;
		cred.oc_cap.cred	= oi->one_comp.cred;

		nfserr = pnfs_osd_xdr_encode_layout_cred(xdr, &cred);
		if (unlikely(nfserr)) {
			EXOFS_DBGMSG("(0x%lx) nfserr=%u total=%u encoded=%u\n",
				     inode->i_ino, nfserr, layout.olo_num_comps,
				     i);
			goto out;
		}
	}

	/* Add to per file_pointer and per_inode layout list, if it failed
	 * it means a write-layout without callback channel.
	 */
	pnfs_layout->seg = res->segment;
	pnfs_layout->caps = NULL /*cap_res->cap*/;
	pnfs_layout->clientid = clientid;

	pnfs_lo_add2file(pnfs_layout, pnfs_file, oi, recall_file_info);

	res->fsal_seg_data = pnfs_layout;
	res->return_on_close = 1;

out:
	if (unlikely(nfserr))
		EXOFS_DBGMSG("(0x%lx) nfserr=%u xdr_bytes=%zu\n",
			  inode->i_ino, nfserr, exp_xdr_qbytes(xdr->p - start));
	return nfserr;
}

/* NOTE: inode mutex must NOT be held */
static int
_pkc_pnfs_layout_commit(
	pan_fs_client_cache_pannode_t *oi,
	struct exp_xdr_stream *xdr,
	const struct fsal_layoutcommit_arg *args,
	struct fsal_layoutcommit_res *res)
{
	struct inode *inode = &oi->vfs_inode;
	struct timespec mtime;
	loff_t i_size;
	int in_recall;
	int ret;
	struct pnfs_osd_layoutupdate lou;

	/* In case of a recall we ignore the new size and mtime since they
	 * are going to be changed again by truncate, and since we cannot take
	 * the inode lock in that case.
	 */
	in_recall = test_bit(OBJ_IN_LAYOUT_RECALL, &oi->i_flags);
	if (in_recall) {
		EXOFS_DBGMSG("(0x%lx) commit was called during recall\n",
			     inode->i_ino);
		return 0;
	}

	/* NOTE: I would love to call inode_setattr here
	 *	 but i cannot since this will cause an eventual vmtruncate,
	 *	 which will cause a layout_recall. So open code the i_size
	 *	 and mtime/atime changes under i_mutex.
	 */
	mutex_lock_nested(&inode->i_mutex, I_MUTEX_NORMAL);

	if (args->time_changed) {
		mtime.tv_sec = args->new_time.seconds;
		mtime.tv_nsec = args->new_time.nseconds;

		/* layout commit may only make time bigger, since there might
		 * be reordering of the notifications and it might arrive after
		 * A local change.
		 * TODO: if mtime > ctime then we know set_attr did an mtime
		 * in the future. and we can let this update through
		 */
		if (0 <= timespec_compare(&mtime, &inode->i_mtime))
			mtime = inode->i_mtime;
	} else {
		mtime = current_fs_time(inode->i_sb);
	}

	/* TODO: Will below work? since mark_inode_dirty has it's own
	 *       Time handling
	 */
	inode->i_atime = inode->i_mtime = mtime;

	i_size = i_size_read(inode);
	if (args->new_offset) {
		loff_t new_size = args->last_write + 1;

		if (i_size < new_size) {
			i_size_write(inode, i_size = new_size);
			res->size_supplied = 1;
			res->new_size = new_size;
		}
	}
	/* TODO: else { i_size = osd_get_object_length() } */

	ret = pnfs_osd_xdr_decode_layoutupdate(&lou, xdr);
	if (ret) {
		EXOFS_DBGMSG("(0x%lx) Failed to decode layoutupdate: %d\n",
			     inode->i_ino, ret);
		return ret;
	}
	if (lou.dsu_valid) {
		/* Record delta */
		oi->i_dev_size += lou.dsu_delta;
		EXOFS_DBGMSG("(0x%lx) dev_size=0x%llx dsu_delta=0x%llx\n",
				inode->i_ino, oi->i_dev_size, lou.dsu_delta);
	}

	mark_inode_dirty_sync(inode);

	mutex_unlock(&inode->i_mutex);
	EXOFS_DBGMSG("(0x%lx) i_size=0x%llx lcp->off=0x%llx\n",
		     inode->i_ino, i_size, args->last_write);
	return 0;
}

static void exofs_handle_error(struct pnfs_osd_ioerr *ioerr)
{
	EXOFS_ERR("exofs_handle_error: errno=%d is_write=%d obj=0x%llx "
		  "offset=0x%llx length=0x%llx\n",
		  ioerr->oer_errno, ioerr->oer_iswrite,
		  _LLU(ioerr->oer_component.oid_object_id),
		  _LLU(ioerr->oer_comp_offset),
		  _LLU(ioerr->oer_comp_length));
}

static int
_pkc_pnfs_layout_return(
	pan_fs_client_cache_pannode_t *oi,
	struct pnfs_file *pnfs_file,
	struct exp_xdr_stream *xdr,
	const struct fsal_layoutreturn_arg *args)
{
	struct inode *inode = &oi->vfs_inode;
	struct pnfs_osd_ioerr ioerr;

	EXOFS_DBGMSG("(0x%lx) lo_cookie=%p cb_cookie=%p empty=%zd body_len %zd\n",
		     inode->i_ino, args->fsal_seg_data, args->recall_cookies[0],
		     args->ncookies, xdr->p - xdr->end);

	while (pnfs_osd_xdr_decode_ioerr(&ioerr, xdr))
		exofs_handle_error(&ioerr);

	if (args->return_type != LAYOUTRETURN4_FILE) {
		EXOFS_DBGMSG("return_type(%d) != LAYOUTRETURN4_FILE \n",
			     args->return_type);
	}

	/* If args->fsal_seg_data is NULL it's a parial return */
	if (args->fsal_seg_data || args->ncookies) {
		struct pnfs_layout *lo = args->fsal_seg_data;
		struct pnfs_recall *recall = (void *)args->recall_cookies[0];
		struct completion *waiter = NULL;

		if (recall) /* recall will dealocate below */
			waiter = recall->waiter;

		pnfs_lo_return(lo, pnfs_file, oi, recall);

		if (waiter)
			complete(waiter);
	}

	return 0;
}

static int _pkc_pnfs_device_info(
	pan_fs_client_cache_pannode_t *oi,
	struct exp_xdr_stream *xdr, u32 layout_type,
	const struct pnfs_deviceid *devid)
{
	struct inode *inode = &oi->vfs_inode;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct pnfs_osd_deviceaddr devaddr;
	struct exofs_dev *edev;
	const struct osd_dev_info *odi;
	u64 devno = devid->devid;
	__be32 *start;
	int err;

	memset(&devaddr, 0, sizeof(devaddr));

	if (unlikely(devno >= sbi->oc.numdevs)) {
		EXOFS_DBGMSG("Error: Device((%llx,%llx) does not exist\n",
			     devid->export_id, devno);
		return -ENODEV;
	}

	edev = container_of(sbi->oc.ods[devno], typeof(*edev), ored);
	odi = osduld_device_info(edev->ored.od);

	devaddr.oda_systemid.len = odi->systemid_len;
	devaddr.oda_systemid.data = (void *)odi->systemid; /* !const cast */

	devaddr.oda_osdname.len = odi->osdname_len ;
	devaddr.oda_osdname.data = (void *)odi->osdname;/* !const cast */

	devaddr.oda_targetaddr.ota_available = OBJ_OTA_AVAILABLE;
	devaddr.oda_targetaddr.ota_netaddr.r_addr.data = (void *)edev->uri;
	devaddr.oda_targetaddr.ota_netaddr.r_addr.len = edev->urilen;

	/* skip opaque size, will be filled-in later */
	start = exp_xdr_reserve_qwords(xdr, 1);
	if (!start) {
		err = -ETOOSMALL;
		goto err;
	}

	err = pnfs_osd_xdr_encode_deviceaddr(xdr, &devaddr);
	if (err) {
		err = -ETOOSMALL;
		goto err;
	}

	exp_xdr_encode_opaque_len(start, xdr->p);

	EXOFS_DBGMSG("xdr_bytes=%Zu devid=(%llx,%llx) osdname-%s\n",
		     exp_xdr_qbytes(xdr->p - start), devid->export_id, devno,
		     odi->osdname);
	return 0;

err:
	EXOFS_DBGMSG("Error: err=%d at_byte=%zu\n",
		     err, exp_xdr_qbytes(xdr->p - start));
	return err;
}

int exofs_inode_recall_layout(struct inode *inode, enum layoutiomode4 io_mode,
			      exofs_recall_fn todo, u64 todo_data)
{
	struct exofs_i_info *oi = exofs_i(inode);
	DECLARE_COMPLETION_ONSTACK(wait);
	int error = 0;

	set_bit(OBJ_IN_LAYOUT_RECALL, &oi->i_flags);

	for (;;) {
		EXOFS_DBGMSG("(0x%lx) has_layout issue a recall\n",
			     inode->i_ino);
		error = cb_layout_recall(oi, io_mode, 0, NFS4_MAX_UINT64,
					 &wait);
		switch (error) {
		case 0:
		case -EAGAIN:
			break;
		case -ENOENT:
			goto exec;
		default:
			goto err;
		}

		error = wait_for_completion_interruptible(&wait);
		if (error)
			goto err;
	}

exec:
	error = todo(inode, todo_data);

err:
	clear_bit(OBJ_IN_LAYOUT_RECALL, &oi->i_flags);
	EXOFS_DBGMSG("(0x%lx) return=>%d\n", inode->i_ino, error);
	return error;
}

void exofs_init_export(struct super_block *sb)
{
/*	sb->s_pnfs_op = &exofs_pnfs_ops;*/
}

// =============================================================================

#define IOCTL_COPYIN(dst, src) \
	copy_from_user(dst, src, sizeof(typeof(*dst)))

#define IOCTL_COPYOUT(dst, src) \
	copy_to_user(dst, src, sizeof(typeof(*dst)));

#define IOCTL_COPYOUT_ARRAY(dst, src, num) \
	copy_to_user(dst, src, num * sizeof(typeof(*dst)));

struct k_xdr {
	struct exp_xdr_stream exp_xdr;
	void *alloc_p;
};

inline int IOCTL_XDR(struct k_xdr *k_xdr,
		     struct pan_ioctl_xdr *ioctl_xdr)
{
	EXOFS_DBGMSG("xdr_buf=%p xdr_alloc_len=0x%x\n", ioctl_xdr->xdr_buff, ioctl_xdr->xdr_alloc_len);
	if (!ioctl_xdr->xdr_buff || !ioctl_xdr->xdr_alloc_len) {
		memset(k_xdr, 0, sizeof(*k_xdr));
		return 0;
	}

	if (ioctl_xdr->xdr_len > ioctl_xdr->xdr_alloc_len)
		return -EINVAL;

	k_xdr->alloc_p = kmalloc(ioctl_xdr->xdr_alloc_len, GFP_KERNEL);
	if (!k_xdr->alloc_p)
		return -ENOMEM;
	k_xdr->exp_xdr.p = k_xdr->alloc_p;
	k_xdr->exp_xdr.end =  k_xdr->alloc_p + ioctl_xdr->xdr_alloc_len;
	return 0;
}

inline int IOCTL_XDR_COPYIN(struct k_xdr *k_xdr,
			    struct pan_ioctl_xdr *ioctl_xdr)
{
	return copy_from_user(k_xdr->alloc_p, ioctl_xdr->xdr_buff,
				   ioctl_xdr->xdr_len);
}

inline int IOCTL_XDR_COPYOUT(struct k_xdr *k_xdr,
			     struct pan_ioctl_xdr *ioctl_xdr)
{
	ioctl_xdr->xdr_len = (void *)k_xdr->exp_xdr.p - k_xdr->alloc_p;
	return copy_to_user(ioctl_xdr->xdr_buff, k_xdr->alloc_p,
				    ioctl_xdr->xdr_len);
}

int exofs_pnfs_ioctl(
	pan_fs_client_cache_pannode_t		*pannode,
	struct pnfs_file 			*pnfs_file,
	uint32_t				command,
	void					*data)
{
	int err;
	struct k_xdr xdr = {{0}}; /* garbage collected at out: */
	pan_fs_client_cache_pannode_t *root =
			     exofs_i(pannode->vfs_inode.i_sb->s_root->d_inode);

	/* We only allocate pnfs structures on first use */
	if(!root->pnfs_node) {
		err = pnfs_pannode_init(root);
		if (err) {
			PNFS_DBG("!!!! pnfs_pannode_init => %d\n", err);
			return err;
		}
	}
	if(!pannode->pnfs_node) {
		err = pnfs_pannode_init(pannode);
		if (err) {
			PNFS_DBG("!!!! pnfs_pannode_init => %d\n", err);
			return err;
		}
	}

#define CHK(err) if (err) {printk(KERN_ERR "CHK-err: @%d\n", __LINE__); goto out;}

	switch (command) {
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_LAYOUTGET: {
		struct pan_layoutget_ioctl *plgi = data;
		struct fsal_layoutget_arg arg;
		struct fsal_layoutget_res res;

		err = IOCTL_XDR(&xdr, &plgi->loc_body);		CHK(err);	/* IN/OUT */

		err = IOCTL_COPYIN(&arg, plgi->arg);		CHK(err);	/* IN */
		err = IOCTL_COPYIN(&res, plgi->res);		CHK(err);	/* IN/OUT */

		plgi->hdr.nfsstat = _pnfs_layout_get(pannode, pnfs_file,
						&xdr.exp_xdr, plgi->clientid,
						plgi->recall_file_info,
						&arg, &res);

		err = IOCTL_COPYOUT(plgi->res, &res);		CHK(err);	/* IN/OUT */
		err = IOCTL_XDR_COPYOUT(&xdr, &plgi->loc_body);	CHK(err);	/* IN/OUT */
	}
		break;
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_DEVICEINFO: {

		struct pan_getdeviceinfo_ioctl *pgdii = data;

		err = IOCTL_XDR(&xdr, &pgdii->da_addr_body);	CHK(err);	/* IN/OUT */

		pgdii->hdr.nfsstat = _pkc_pnfs_device_info(pannode, &xdr.exp_xdr,
						pgdii->type, &pgdii->deviceid);

		err = IOCTL_XDR_COPYOUT(&xdr, &pgdii->da_addr_body); CHK(err);	/* IN/OUT */
	}
		break;
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_LAYOUTRETURN: {
		struct pan_layoutreturn_ioctl *plri = data;
		struct fsal_layoutreturn_arg arg;

		err = IOCTL_XDR(&xdr, &plri->lrf_body);		CHK(err);
		err = IOCTL_XDR_COPYIN(&xdr, &plri->lrf_body);	CHK(err);	/*   IN   */
		err = IOCTL_COPYIN(&arg, plri->arg);		CHK(err);	/*   IN   */

		plri->hdr.nfsstat = _pkc_pnfs_layout_return(pannode, pnfs_file,
							    &xdr.exp_xdr, &arg);
	}
		break;
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_LAYOUTCOMMIT: {
		struct pan_layoutcommit_ioctl *plci = data;
		struct fsal_layoutcommit_arg arg;
		struct fsal_layoutcommit_res res = {0};

		err = IOCTL_XDR(&xdr, &plci->lou_body);		CHK(err);
		err = IOCTL_XDR_COPYIN(&xdr, &plci->lou_body);	CHK(err);	/*   IN   */
		err = IOCTL_COPYIN(&arg, plci->arg);		CHK(err);	/*   IN   */

		plci->hdr.nfsstat = _pkc_pnfs_layout_commit(pannode,
						&xdr.exp_xdr, &arg, &res);

		err = IOCTL_COPYOUT(plci->res, &res);		CHK(err);	/*  OUT   */
		err = 0;
	}
		break;
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_LAYOUTRECALL: {
		struct pan_cb_layoutrecall_ioctl *pclri = data;
		enum { EONSTACK = 8U };
		struct pan_cb_layoutrecall_event recall_events[EONSTACK];
		struct pan_cb_layoutrecall_event *cur_user_array;
		bool alow_sleep = true;

		cur_user_array = pclri->events;
		pclri->num_events = 0;

		for(;;) {
			unsigned tocopy = min_t(unsigned,
					pclri->max_events - pclri->num_events,
					EONSTACK);
			int numevents = pnfs_lo_receive_recalls(
							pannode, recall_events,
							tocopy, alow_sleep);

			if (unlikely(numevents < 0)) {
				EXOFS_DBGMSG("!!! pkc_lo_receive_recalls => %d\n",
					     numevents);
				/* This is the wait-interruptable case */
				pclri->hdr.nfsstat = NFS4ERR_LAYOUTTRYLATER;
				/* Stick the break code here for now */
				pclri->hdr.size = numevents;
				err = 0; /* It is allowed */
				goto out;
			} else if (!numevents) {
				/* Ha, last iteration had exactly EONSTACK
				 * events, but stack Queue was empty
				 */
				break;
			}
			/* We had some events don't sleep on zero */
			alow_sleep = false;

			err = IOCTL_COPYOUT_ARRAY(cur_user_array,
						  recall_events, numevents);
			CHK(err);

			cur_user_array += numevents;
			pclri->num_events += numevents;

			if ((numevents < tocopy) ||
			    (pclri->num_events >= pclri->max_events))
				break; /* Done */
		}

		EXOFS_DBGMSG("receive_recalls: => %d\n", pclri->num_events);
		pclri->hdr.nfsstat = NFS4_OK;
		err = 0;
	}
		break;
//////////////////////////////////////////////
	case PAN_FS_CLIENT_PNFS_CANCEL_RECALLS: {
		struct pan_cancel_recalls_ioctl *pcri = data;

		err = 0;
		pcri->hdr.nfsstat = pnfs_lo_cancel_recalls(pannode,
							  pcri->debug_magic);

	}
		break;
//////////////////////////////////////////////

	default:
		/* This is what panfs returns on IOCTL errors */
		return -EINVAL;
	}

out:
	kfree(xdr.alloc_p);
	return err;
}
