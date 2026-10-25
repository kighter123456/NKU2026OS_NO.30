/* lab2: 2414067 - shared address-ordered block operations, private to fit backends. */
#ifndef LAB2_FIT_PMM_H
#define LAB2_FIT_PMM_H
#include <pmm.h>
void fit_init(void);
void fit_init_memmap(struct Page *,size_t);
struct Page *fit_alloc_pages(size_t,bool best);
void fit_free_pages(struct Page *,size_t);
size_t fit_nr_free_pages(void);
#endif
