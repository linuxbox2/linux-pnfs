#ifndef _LIN_PNFS_TYPES_H
#define _LIN_PNFS_TYPES_H

#include <linux/slab.h>
#include <linux/wait.h>

static inline
void *lo_malloc(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

static inline
void *lo_zalloc(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

static inline
void lo_free(void* p)
{
	kfree(p);
}

#ifndef PNFS_DBG
#define PNFS_DBG(fmt, a...) \
	EXOFS_DBGMSG(fmt, ##a)
#endif

/* Some emulated panasas types */
#define pan_spinlock_t spinlock_t
typedef struct exofs_i_info pan_fs_client_cache_pannode_t;

static inline void pan_spinlock_init_name(pan_spinlock_t *sp, const char *name)
{
	spin_lock_init(sp);
}

static inline void pan_spinlock_lock(pan_spinlock_t *sp)
{
	spin_lock(sp);
}

static inline void pan_spinlock_unlock(pan_spinlock_t *sp)
{
	spin_unlock(sp);
}

#endif /* _LIN_PNFS_TYPES_H */
