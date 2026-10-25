/* lab2: 2414067 - B standalone assertions. */
#ifndef LAB2_ASSERT_H
#define LAB2_ASSERT_H
#include <defs.h>
void __panic(const char *file, int line, const char *message) __noreturn;
#define panic(message) __panic(__FILE__, __LINE__, (message))
#define assert(condition) do { if (!(condition)) panic(#condition); } while (0)
#endif
