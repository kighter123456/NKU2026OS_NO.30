#include <stdio.h>
#include <string.h>
#include <sbi.h>
#include <dtb.h>
#include <pmm.h>
#include <selftest.h>
/* lab2: 2414067 - original a0/a1 pass through entry.S unchanged. */
int kern_init(uintptr_t hartid, uintptr_t dtb_pa) __attribute__((noreturn));

int kern_init(uintptr_t hartid, uintptr_t dtb_pa) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
    dtb_init(hartid,dtb_pa);
    pmm_init();
    run_selftests();
   while (1)
        ;
}
