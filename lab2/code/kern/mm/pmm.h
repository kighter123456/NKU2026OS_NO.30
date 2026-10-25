/* B/C contract: initialize after boot Sv39 and DTB discovery; addresses are high VA. */
#ifndef LAB2_PMM_H
#define LAB2_PMM_H
#include <memlayout.h>
#include <mmu.h>
#include <assert.h>
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *,size_t);
    struct Page *(*alloc_pages)(size_t);
    void (*free_pages)(struct Page *,size_t);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern uint64_t va_pa_offset;
extern uintptr_t free_begin, managed_end;
extern const struct pmm_manager *pmm_manager;
extern const struct pmm_manager default_pmm_manager, best_fit_pmm_manager, buddy_pmm_manager;
uintptr_t paddr(const void *);
void *kaddr(uintptr_t);
#define PADDR(kva) paddr((const void *)(kva))
#define KADDR(pa) kaddr((uintptr_t)(pa))
uintptr_t page2pa(const struct Page *);
struct Page *pa2page(uintptr_t);
#define page2kva(page) KADDR(page2pa(page))
void pmm_init(void);
struct Page *alloc_pages(size_t);
void free_pages(struct Page *,size_t);
size_t nr_free_pages(void);
size_t allocation_pages(const struct Page *);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages((p),1)
/* Backend helpers validate the complete range before changing any state. */
void pmm_range(struct Page *,size_t);
void pmm_mark_allocated(struct Page *,size_t,size_t);
void pmm_mark_free(struct Page *,size_t);
void pmm_check(void);
#endif
