/* lab2: 2414067 */
#ifndef LAB2_DTB_H
#define LAB2_DTB_H
#include <defs.h>
struct memory_info { uintptr_t base; size_t size; };
int dtb_parse(const void *blob, size_t available, struct memory_info *out);
void dtb_init(uintptr_t hartid, uintptr_t dtb_pa);
uintptr_t get_memory_base(void);
size_t get_memory_size(void);
#endif
