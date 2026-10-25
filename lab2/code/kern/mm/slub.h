/* lab2: 2414067 */
#ifndef LAB2_SLUB_H
#define LAB2_SLUB_H
#include <defs.h>
void *kmalloc(size_t);
void kfree(void *);
void slub_check(void);
#endif
