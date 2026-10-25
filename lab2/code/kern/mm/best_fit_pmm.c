/* lab2: 2414067 - complete Best-Fit registration; no course tests were supplied. */
#include <fit_pmm.h>
static struct Page *best_alloc(size_t n) {return fit_alloc_pages(n,1);}
const struct pmm_manager best_fit_pmm_manager={
    .name="best_fit_pmm_manager",.init=fit_init,.init_memmap=fit_init_memmap,
    .alloc_pages=best_alloc,.free_pages=fit_free_pages,
    .nr_free_pages=fit_nr_free_pages,.check=pmm_check
};
