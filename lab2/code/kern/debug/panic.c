/* lab2: 2414067 */
#include <assert.h>
#include <stdio.h>
void __panic(const char *file, int line, const char *message) {
    cprintf("\n[PANIC] %s:%d: %s\n", file, line, message);
    for (;;) __asm__ volatile("wfi");
}
