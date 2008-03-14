/*
 * Virtual memory area descriptors.
 *
 * Copyright (C) 2007, 2008 Bahadir Balban
 */
#ifndef __VM_AREA_H__
#define __VM_AREA_H__

#include <stdio.h>
#include <task.h>
#include <l4/macros.h>
#include <l4/config.h>
#include <l4/types.h>
#include <arch/mm.h>
#include <lib/spinlock.h>

/* Protection flags */
#define VM_NONE				(1 << 0)
#define VM_READ				(1 << 1)
#define VM_WRITE			(1 << 2)
#define VM_EXEC				(1 << 3)
#define VM_PROT_MASK			(VM_READ | VM_WRITE | VM_EXEC)

/* Shared copy of a file */
#define VMA_SHARED			(1 << 4)
/* VMA that's not file-backed, always maps devzero as VMA_COW */
#define VMA_ANONYMOUS			(1 << 5)
/* Private copy of a file */
#define VMA_PRIVATE			(1 << 6)
/* Copy-on-write semantics */
#define VMA_FIXED			(1 << 7)

/* Defines the type of file. A device file? Regular file? One used at boot? */
enum VM_FILE_TYPE {
	VM_FILE_DEVZERO = 1,
	VM_FILE_REGULAR,
	VM_FILE_BOOTFILE,
};

/* Defines the type of object. A file? Just a standalone object? */
#define VM_OBJ_SHADOW		(1 << 8) /* Anonymous pages, swap_pager */
#define VM_OBJ_FILE		(1 << 9) /* VFS file and device pages */

struct page {
	int refcnt;		/* Refcount */
	struct spinlock lock;	/* Page lock. */
	struct list_head list;  /* For list of a vm_object's in-memory pages */
	struct vm_object *owner;/* The vm_object the page belongs to */
	unsigned long virtual;	/* If refs >1, first mapper's virtual address */
	unsigned int flags;	/* Flags associated with the page. */
	unsigned long offset;	/* The offset page resides in its owner */
};
extern struct page *page_array;

/*
 * A suggestion to how a non-page_array (i.e. a device)
 * page could tell its physical address.
 */
struct devpage {
	struct page page;
	unsigned long phys;
};

#define page_refcnt(x)		((x)->count + 1)
#define virtual(x)		((x)->virtual)
#define phys_to_page(x)		(page_array + __pfn(x))
#define page_to_phys(x)		__pfn_to_addr((((void *)x) - \
					       (void *)page_array) \
					      / sizeof(struct page))

/* Fault data specific to this task + ptr to kernel's data */
struct fault_data {
	fault_kdata_t *kdata;		/* Generic data flonged by the kernel */
	unsigned int reason;		/* Generic fault reason flags */
	unsigned int address;		/* Aborted address */
	struct vm_area *vma;		/* Inittask-related fault data */
	struct tcb *task;		/* Inittask-related fault data */
};

struct vm_pager_ops {
	struct page *(*page_in)(struct vm_object *vm_obj,
				unsigned long pfn_offset);
	struct page *(*page_out)(struct vm_object *vm_obj,
				 unsigned long pfn_offset);
	int (*release_pages)(struct vm_object *vm_obj);
};

/* Describes the pager task that handles a vm_area. */
struct vm_pager {
	struct vm_pager_ops ops;	/* The ops the pager does on area */
};


/* TODO:
 * How to distinguish different devices handling page faults ???
 * A possible answer:
 *
 * If they are not mmap'ed, this is handled by the vfs calling that file's
 * specific operations (e.g. even calling the device process). If they're
 * mmap'ed, they adhere to a standard mmap_device structure kept in
 * vm_file->priv_data. This is used by the device pager to map those pages.
 */

/*
 * Describes the in-memory representation of a resource. This could
 * point at a file or another resource, e.g. a device area, swapper space,
 * the anonymous internal state of a process, etc. This covers more than
 * just files, e.g. during a fork, captures the state of internal shared
 * copy of private pages for a process, which is really not a file.
 */
struct vm_object {
	int npages;		    /* Number of pages in memory */
	int refcnt;		    /* Number of shadows (or vmas) that refer */
	struct list_head shadowers; /* List of vm objects that shadow this one */
	struct vm_object *orig_obj; /* Original object that this one shadows */
	unsigned int flags;	    /* Defines the type and flags of the object */
	struct list_head list;	    /* List of all vm objects in memory */
	struct vm_pager *pager;	    /* The pager for this object */
	struct list_head page_cache;/* List of in-memory pages */
};

/* In memory representation of either a vfs file, a device. */
struct vm_file {
	unsigned long vnum;
	unsigned long length;
	unsigned int type;
	struct list_head list;
	struct vm_object vm_obj;
	void *priv_data;	/* Device pagers use to access device info */
};

/* To create per-vma vm_object lists */
struct vm_obj_link {
	struct list_head list;
	struct list_head shref;	/* Ref to shadowers by original objects */
	struct vm_object *obj;
};

#define vm_object_to_file(obj) container_of(obj, struct vm_file, vm_obj)

/*
 * Describes a virtually contiguous chunk of memory region in a task. It covers
 * a unique virtual address area within its task, meaning that it does not
 * overlap with other regions in the same task. The region could be backed by a
 * file or various other resources. This is managed by the region's pager.
 *
 * COW: Upon copy-on-write, each copy-on-write instance creates a shadow of the
 * original vm object which supersedes the original vm object with its copied
 * modified pages. This creates a stack of shadow vm objects, where the top
 * object's copy of pages supersede the ones lower in the stack.
 */
struct vm_area {
	struct list_head list;		/* Per-task vma list */
	struct list_head vm_obj_list;	/* Head for vm_object list. */
	unsigned long pfn_start;	/* Region start virtual pfn */
	unsigned long pfn_end;		/* Region end virtual pfn, exclusive */
	unsigned long flags;		/* Protection flags. */
	unsigned long file_offset;	/* File offset in pfns */
};

static inline struct vm_area *find_vma(unsigned long addr,
				       struct list_head *vm_area_list)
{
	struct vm_area *vma;
	unsigned long pfn = __pfn(addr);

	list_for_each_entry(vma, vm_area_list, list)
		if ((pfn >= vma->pfn_start) && (pfn < vma->pfn_end))
			return vma;
	return 0;
}

/* Adds a page to its vm_objects's page cache in order of offset. */
int insert_page_olist(struct page *this, struct vm_object *vm_obj);

/* Find a page in page cache via page offset */
struct page *find_page(struct vm_object *obj, unsigned long pfn);

/* Pagers */
extern struct vm_pager file_pager;
extern struct vm_pager bootfile_pager;
extern struct vm_pager devzero_pager;
extern struct vm_pager swap_pager;

/* vm object and vm file lists */
extern struct list_head vm_object_list;

/* vm object link related functions */
struct vm_obj_link *vm_objlink_create(void);
struct vm_obj_link *vma_next_link(struct list_head *link,
				  struct list_head *head);

/* vm file and object initialisation */
struct vm_file *vm_file_alloc_init(void);
struct vm_object *vm_object_alloc_init(void);
struct vm_object *vm_object_create(void);
struct vm_file *vm_file_create(void);
int vm_object_delete(struct vm_object *vmo);

/* Main page fault entry point */
void page_fault_handler(l4id_t tid, fault_kdata_t *fkdata);

#endif /* __VM_AREA_H__ */
