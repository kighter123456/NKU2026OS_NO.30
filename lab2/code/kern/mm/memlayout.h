#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

#define KSTACKPAGE          2                           // # of pages in kernel stack
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack

/* lab2: 2414067. B validates in Bare mode; A/C must set this before paging. */
#ifndef KERNEL_VA_PA_OFFSET
#define KERNEL_VA_PA_OFFSET 0ULL
#endif
#ifndef __ASSEMBLER__
#include <defs.h>
#include <list.h>
#define DRAM_BASE 0x80000000ULL
#define PHYSTOP   0x88000000ULL
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
    /* B ownership metadata: actual span and original request at allocated head. */
    size_t alloc_span, requested;
};
#define PG_reserved 1ULL
#define PG_property 2ULL
#define PG_allocated 4ULL
#define PageReserved(p) ((p)->flags & PG_reserved)
#define PageProperty(p) ((p)->flags & PG_property)
#define PageAllocated(p) ((p)->flags & PG_allocated)
#define SetPageReserved(p) ((p)->flags |= PG_reserved)
#define ClearPageReserved(p) ((p)->flags &= ~PG_reserved)
#define SetPageProperty(p) ((p)->flags |= PG_property)
#define ClearPageProperty(p) ((p)->flags &= ~PG_property)
#define le2page(le, member) to_struct(le, struct Page, member)
typedef struct { list_entry_t free_list; size_t nr_free; } free_area_t;
#endif

#endif /* !__KERN_MM_MEMLAYOUT_H__ */
