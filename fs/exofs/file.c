/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
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

/* exofs_file_fsync - flush the inode to disk
 *
 *   Note, in exofs all metadata is written as part of inode, regardless.
 *   The writeout is synchronous
 */
static int exofs_file_fsync(struct file *filp, loff_t start, loff_t end,
			    int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	int ret;

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;

	mutex_lock(&inode->i_mutex);
	ret = sync_inode_metadata(filp->f_mapping->host, 1);
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static int exofs_flush(struct file *file, fl_owner_t id)
{
	int ret = vfs_fsync(file, 0);
	/* TODO: Flush the OSD target */
	return ret;
}

int exofs_file_open(struct inode * inode, struct file * filp)
{
	filp->private_data = kzalloc(sizeof(struct pkc_pnfs_file), GFP_KERNEL);
	if (unlikely(!filp->private_data))
		return -ENOMEM;

	pkc_pnfs_file_init(filp->private_data);
	return 0;
}

int exofs_release_file(struct inode *inode, struct file *filp)
{
	pkc_pnfs_file_close(filp->private_data, exofs_i(inode));
	kfree(filp->private_data);

	return 0;
}

long exofs_ioctl(struct file *filp, unsigned int cmd, unsigned long param)
{

	/* determine space needed for the ioctl arguments.
	 * We assume that if _IOC_SIZE is not zero that param is a buffer
	 * of size _IOC_SIZE(cmd) bytes
	 */
	void *kernel_args = NULL;
	int args_size = _IOC_SIZE(cmd);
	int ret;


	if (args_size > 0) {
		kernel_args = kmalloc(args_size, GFP_KERNEL);
		if (unlikely(!kernel_args))
			return -ENOMEM;

		if (_IOC_DIR(cmd) & _IOC_READ) {
			ret = copy_from_user(kernel_args, (void *)param,
					     args_size);
			if (unlikely(ret))
				return ret;
		}
	}

	ret = pan_fs_client_pnfs_ioctl(exofs_i(filp->f_mapping->host),
					filp->private_data, cmd, kernel_args);

	if (args_size > 0) {
		int ret2;

		if (_IOC_DIR(cmd) & _IOC_WRITE) {
			ret2 = copy_to_user((void *)param, kernel_args,
					    args_size);
			if (unlikely(ret2 && !ret))
				ret = ret2;
		}
		kfree(kernel_args);
	}

/*	EXOFS_DBGMSG("cmd=0x%x, args_size=%d param=0x%lX => %d\n",
		     cmd, args_size, param, ret);*/
	return ret;
}

long exofs_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long param)
{
	return exofs_ioctl(filp, cmd, param);
}

const struct file_operations exofs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= exofs_file_open,
	.unlocked_ioctl = exofs_ioctl,
	.compat_ioctl	= exofs_compat_ioctl,
	.release	= exofs_release_file,
	.fsync		= exofs_file_fsync,
	.flush		= exofs_flush,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

const struct inode_operations exofs_file_inode_operations = {
	.setattr	= exofs_setattr,
};
