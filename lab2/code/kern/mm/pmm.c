/* lab2: 2414067 - DTB -> Page descriptors -> selected PMM. */
#include <pmm.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>

struct Page *pages;
size_t npage;
const size_t nbase=DRAM_BASE/PGSIZE;
uint64_t va_pa_offset;
uintptr_t free_begin, managed_end;
const struct pmm_manager *pmm_manager;

uintptr_t paddr(const void *kva) {
    uintptr_t v=(uintptr_t)kva;
    assert(v>=va_pa_offset);
    uintptr_t pa=v-va_pa_offset;
    assert(pa>=DRAM_BASE && pa<managed_end);
    return pa;
}
void *kaddr(uintptr_t pa) {
    assert(pa>=DRAM_BASE && pa<managed_end && pa<=UINT64_MAX-va_pa_offset);
    return (void *)(pa+va_pa_offset);
}
uintptr_t page2pa(const struct Page *p) {
    uintptr_t v=(uintptr_t)p, first=(uintptr_t)pages;
    assert(pages && v>=first && (v-first)%sizeof(*p)==0);
    size_t index=(v-first)/sizeof(*p);
    assert(index<npage-nbase);
    return (index+nbase)*PGSIZE;
}
struct Page *pa2page(uintptr_t pa) {
    assert(pa>=DRAM_BASE && pa<managed_end);
    return pages+(pa/PGSIZE-nbase);
}
void pmm_range(struct Page *base,size_t n) {
    assert(n>0);
    uintptr_t pa=page2pa(base);
    assert(pa>=free_begin && n<=(managed_end-pa)/PGSIZE);
}
void pmm_mark_allocated(struct Page *base,size_t span,size_t requested) {
    pmm_range(base,span);
    for (size_t i=0;i<span;++i) assert(!PageReserved(base+i) && !PageAllocated(base+i));
    for (size_t i=0;i<span;++i) {
        base[i].flags=PG_allocated; base[i].property=0;
        base[i].alloc_span=base[i].requested=0;
    }
    base->alloc_span=span; base->requested=requested;
}
void pmm_mark_free(struct Page *base,size_t n) {
    pmm_range(base,n);
    for (size_t i=0;i<n;++i) assert(PageAllocated(base+i) && !PageReserved(base+i));
    for (size_t i=0;i<n;++i) {
        base[i].flags=0; base[i].property=0; base[i].ref=0;
        base[i].alloc_span=base[i].requested=0;
        list_init(&base[i].page_link);
    }
}
struct Page *alloc_pages(size_t n) {
    assert(n>0);
    return pmm_manager->alloc_pages(n);
}
void free_pages(struct Page *base,size_t n) {pmm_manager->free_pages(base,n);}
size_t nr_free_pages(void) {return pmm_manager->nr_free_pages();}
size_t allocation_pages(const struct Page *p) {
    pmm_range((struct Page *)p,1);
    assert(PageAllocated(p) && p->alloc_span>0);
    pmm_range((struct Page *)p,p->alloc_span);
    for (size_t i=0;i<p->alloc_span;++i) assert(PageAllocated(p+i));
    return p->alloc_span;
}
static void page_init(void) {
    uintptr_t base=get_memory_base(); size_t size=get_memory_size();
    assert(base==DRAM_BASE && size>0 && size<=UINT64_MAX-base);
    uintptr_t detected_end=base+size;
    managed_end=ROUNDDOWN(detected_end<PHYSTOP ? detected_end : PHYSTOP,PGSIZE);
    assert(managed_end>DRAM_BASE);
    npage=managed_end/PGSIZE;
    va_pa_offset=KERNEL_VA_PA_OFFSET;
    extern char end[];
    uintptr_t desc_va=ROUNDUP((uintptr_t)end,PGSIZE);
    uintptr_t desc_pa=desc_va-va_pa_offset;
    size_t count=npage-nbase;
    assert(desc_pa>=0x80200000 && desc_pa<managed_end);
    assert(count<=(managed_end-desc_pa)/sizeof(struct Page));
    pages=(struct Page *)desc_va;
    uintptr_t desc_end=desc_pa+count*sizeof(struct Page);
    free_begin=ROUNDUP(desc_end,PGSIZE);
    assert(free_begin<=managed_end);
    memset(pages,0,count*sizeof(struct Page));
    for (size_t i=0;i<count;++i) {
        SetPageReserved(pages+i); list_init(&pages[i].page_link);
    }
    cprintf("MANAGED: [0x%016lx,0x%016lx) %lu MiB%s\n",base,managed_end,
            (managed_end-base)>>20,detected_end>PHYSTOP ? " (capped at 128 MiB)" : "");
    cprintf("LAYOUT: descriptors=[0x%016lx,0x%016lx) Page=%lu bytes\n",
            desc_pa,desc_end,sizeof(struct Page));
    cprintf("FREE: [0x%016lx,0x%016lx) pages=%lu\n",free_begin,managed_end,
            (managed_end-free_begin)/PGSIZE);
    if (free_begin<managed_end)
        pmm_manager->init_memmap(pa2page(free_begin),(managed_end-free_begin)/PGSIZE);
}
void pmm_init(void) {
#if PMM_BACKEND == 0
    pmm_manager=&default_pmm_manager;
#elif PMM_BACKEND == 2
    pmm_manager=&buddy_pmm_manager;
#else
    pmm_manager=&best_fit_pmm_manager;
#endif
    cprintf("memory management: %s (Sv39 integrated)\n",pmm_manager->name);
    pmm_manager->init();
    page_init();
    assert(nr_free_pages()==(managed_end-free_begin)/PGSIZE);

    /* The guide prints the boot page table addresses after PMM setup. */
    extern char boot_page_table_sv39[];
    uintptr_t *satp_virtual=(uintptr_t *)boot_page_table_sv39;
    uintptr_t satp_physical=PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\n",(uintptr_t)satp_virtual);
    cprintf("satp physical address: 0x%016lx\n",satp_physical);
}
