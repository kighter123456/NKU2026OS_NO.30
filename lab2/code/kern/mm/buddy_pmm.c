/* lab2: 2414067 - physical-PFN-aligned Buddy with original-request validation. */
#include <pmm.h>
#define MAX_ORDER 15
static list_entry_t orders[MAX_ORDER+1];
static size_t available;
static void buddy_init(void) {
    for (unsigned o=0;o<=MAX_ORDER;++o) list_init(&orders[o]);
    available=0;
}
static void add(struct Page *p,unsigned order) {
    p->property=order; SetPageProperty(p);
    list_add(&orders[order],&p->page_link);
}
static void remove_block(struct Page *p) {
    list_del(&p->page_link); ClearPageProperty(p); p->property=0;
}
static void buddy_init_memmap(struct Page *base,size_t n) {
    pmm_range(base,n);
    for (size_t i=0;i<n;++i) assert(PageReserved(base+i));
    for (size_t i=0;i<n;++i) base[i].flags=0;
    available+=n;
    while (n) {
        size_t pfn=page2pa(base)/PGSIZE; unsigned o=0;
        while (o<MAX_ORDER && ((size_t)1<<(o+1))<=n && !(pfn&(((size_t)1<<(o+1))-1))) ++o;
        add(base,o); base+=(size_t)1<<o; n-=(size_t)1<<o;
    }
}
static struct Page *buddy_alloc(size_t n) {
    if (n>available || n>((size_t)1<<MAX_ORDER)) return NULL;
    unsigned want=0;
    while (((size_t)1<<want)<n) ++want;
    unsigned o=want;
    while (o<=MAX_ORDER && list_empty(&orders[o])) ++o;
    if (o>MAX_ORDER) return NULL;
    struct Page *p=le2page(orders[o].next,page_link); remove_block(p);
    while (o>want) {--o;add(p+((size_t)1<<o),o);}
    size_t span=(size_t)1<<want;
    pmm_mark_allocated(p,span,n); available-=span;
    return p;
}
static void buddy_free(struct Page *base,size_t n) {
    pmm_range(base,n);
    assert(PageAllocated(base) && base->alloc_span && base->requested==n);
    size_t span=base->alloc_span;
    pmm_range(base,span);
    unsigned o=0; while (((size_t)1<<o)<span) ++o;
    pmm_mark_free(base,span); available+=span;
    while (o<MAX_ORDER) {
        size_t buddy_pfn=(page2pa(base)/PGSIZE)^((size_t)1<<o);
        uintptr_t pa=buddy_pfn*PGSIZE;
        if (pa<free_begin || pa>=managed_end || ((size_t)1<<o)>(managed_end-pa)/PGSIZE) break;
        struct Page *other=pa2page(pa);
        if (!PageProperty(other) || other->property!=o) break;
        remove_block(other);
        if (other<base) base=other;
        ++o;
    }
    add(base,o);
}
static size_t buddy_count(void) {return available;}
const struct pmm_manager buddy_pmm_manager={
    .name="buddy_pmm_manager",.init=buddy_init,.init_memmap=buddy_init_memmap,
    .alloc_pages=buddy_alloc,.free_pages=buddy_free,
    .nr_free_pages=buddy_count,.check=pmm_check
};
