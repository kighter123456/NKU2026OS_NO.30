/* lab2: 2414067 - First-Fit and common address-ordered coalescing. */
#include <fit_pmm.h>
static free_area_t area;
void fit_init(void) {list_init(&area.free_list);area.nr_free=0;}
static void insert_block(struct Page *p,size_t n) {
    list_entry_t *h=&area.free_list,*at=h->next;
    while (at!=h && le2page(at,page_link)<p) at=at->next;
    p->property=n; SetPageProperty(p);
    list_add_before(at,&p->page_link);
    if (p->page_link.prev!=h) {
        struct Page *prev=le2page(p->page_link.prev,page_link);
        if (prev+prev->property==p) {
            prev->property+=p->property;
            list_del(&p->page_link); ClearPageProperty(p); p->property=0; p=prev;
        }
    }
    if (p->page_link.next!=h) {
        struct Page *next=le2page(p->page_link.next,page_link);
        if (p+p->property==next) {
            p->property+=next->property;
            list_del(&next->page_link); ClearPageProperty(next); next->property=0;
        }
    }
}
void fit_init_memmap(struct Page *base,size_t n) {
    pmm_range(base,n);
    for (size_t i=0;i<n;++i) assert(PageReserved(base+i));
    for (size_t i=0;i<n;++i) base[i].flags=0;
    insert_block(base,n); area.nr_free+=n;
}
struct Page *fit_alloc_pages(size_t n,bool best) {
    if (n>area.nr_free) return NULL;
    list_entry_t *h=&area.free_list;
    struct Page *chosen=NULL;
    for (list_entry_t *at=h->next;at!=h;at=at->next) {
        struct Page *p=le2page(at,page_link);
        if (p->property>=n && (!chosen || p->property<chosen->property)) {
            chosen=p;
            if (!best || p->property==n) break;
        }
    }
    if (!chosen) return NULL;
    size_t length=chosen->property;
    list_del(&chosen->page_link);
    if (length>n) insert_block(chosen+n,length-n);
    pmm_mark_allocated(chosen,n,n); area.nr_free-=n;
    return chosen;
}
void fit_free_pages(struct Page *base,size_t n) {
    pmm_mark_free(base,n); insert_block(base,n); area.nr_free+=n;
}
size_t fit_nr_free_pages(void) {return area.nr_free;}
static struct Page *first_alloc(size_t n) {return fit_alloc_pages(n,0);}
const struct pmm_manager default_pmm_manager={
    .name="default_pmm_manager",.init=fit_init,.init_memmap=fit_init_memmap,
    .alloc_pages=first_alloc,.free_pages=fit_free_pages,
    .nr_free_pages=fit_nr_free_pages,.check=pmm_check
};
