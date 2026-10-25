/* lab2: 2414067 - teaching SLUB: page caches + per-page object free lists. */
#include <slub.h>
#include <pmm.h>
#include <string.h>
#define SLAB_MAGIC 0x534c55424c414232ULL
#define LARGE_MAGIC 0x4c415247454c4232ULL
#define HEADER_SIZE 128UL
#define PG_slub_owned 8ULL
struct slab {
    uint64_t magic;
    struct slab *next, *prev;
    size_t unit, capacity, used, request_pages;
    void *free;
    uint64_t bitmap[4];
};
_Static_assert(sizeof(struct slab)<=HEADER_SIZE,"slab metadata exceeds aligned header");
static struct slab *caches[8];
static unsigned cache_index(size_t unit) {
    unsigned i=0; while (((size_t)16<<i)<unit) ++i; return i;
}
static void link_slab(struct slab *s,unsigned index) {
    s->prev=NULL; s->next=caches[index];
    if (s->next) s->next->prev=s;
    caches[index]=s;
}
static void unlink_slab(struct slab *s,unsigned index) {
    if (s->prev) s->prev->next=s->next; else caches[index]=s->next;
    if (s->next) s->next->prev=s->prev;
}
void *kmalloc(size_t size) {
    if (!size) return NULL;
    if (size>UINT64_MAX-HEADER_SIZE-(PGSIZE-1)) return NULL;
    if (size>2048) {
        size_t n=(size+HEADER_SIZE+PGSIZE-1)/PGSIZE;
        struct Page *p=alloc_pages(n);
        if (!p) return NULL;
        struct slab *s=page2kva(p);
        memset(s,0,HEADER_SIZE); s->magic=LARGE_MAGIC; s->request_pages=n;
        p->flags|=PG_slub_owned;
        return (char *)s+HEADER_SIZE;
    }
    unsigned index=cache_index(size);
    size_t unit=(size_t)16<<index;
    struct slab *s=caches[index];
    while (s && !s->free) s=s->next;
    if (!s) {
        struct Page *p=alloc_page(); if (!p) return NULL;
        s=page2kva(p); memset(s,0,HEADER_SIZE);
        p->flags|=PG_slub_owned;
        s->magic=SLAB_MAGIC; s->unit=unit;
        s->capacity=(PGSIZE-HEADER_SIZE)/unit;
        for (size_t i=s->capacity;i>0;--i) {
            void *obj=(char *)s+HEADER_SIZE+(i-1)*unit;
            *(void **)obj=s->free; s->free=obj;
        }
        link_slab(s,index);
    }
    void *obj=s->free;
    s->free=*(void **)obj;
    size_t slot=((char *)obj-(char *)s-HEADER_SIZE)/unit;
    assert(!(s->bitmap[slot/64]&(1ULL<<(slot%64))));
    s->bitmap[slot/64]|=1ULL<<(slot%64); ++s->used;
    return obj;
}
void kfree(void *ptr) {
    if (!ptr) return;
    uintptr_t pa=PADDR(ptr);
    assert(pa>=free_begin);
    struct Page *p=pa2page(pa);
    assert(PageAllocated(p) && p->alloc_span>0);
    assert(p->flags&PG_slub_owned);
    struct slab *s=page2kva(p);
    if (s->magic==LARGE_MAGIC) {
        assert(ptr==(char *)s+HEADER_SIZE);
        size_t n=s->request_pages;
        assert(n==p->requested);
        s->magic=0; free_pages(p,n); return;
    }
    assert(s->magic==SLAB_MAGIC);
    size_t offset=(uintptr_t)ptr-(uintptr_t)s;
    assert(offset>=HEADER_SIZE && offset<HEADER_SIZE+s->capacity*s->unit);
    assert((offset-HEADER_SIZE)%s->unit==0);
    size_t slot=(offset-HEADER_SIZE)/s->unit;
    uint64_t bit=1ULL<<(slot%64);
    assert(s->bitmap[slot/64]&bit);
    s->bitmap[slot/64]&=~bit; --s->used;
    *(void **)ptr=s->free; s->free=ptr;
    if (!s->used) {
        unlink_slab(s,cache_index(s->unit));
        s->magic=0; free_page(p);
    }
}
