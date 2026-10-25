#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # t0 := virtual address of the boot page table
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := KERNEL_VA_PA_OFFSET (kernel virtual address - physical address)
    li      t1, KERNEL_VA_PA_OFFSET
    # t0 := physical address of the page table, then its page number
    sub     t0, t0, t1
    srli    t0, t0, PGSHIFT

    # satp := (Sv39 mode 8) | root page number
    li      t1, 8 << 60
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma

    # From here on all addresses are translated by the boot page table.
    lui     sp, %hi(bootstacktop)

    lui     t0, %hi(kern_init)
    addi    t0, t0, %lo(kern_init)
    jr      t0

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:

.section .data
    # Boot page table:
    #   index 2:   VA [0x80000000, 0xc0000000) -> PA [0x80000000, 0xc0000000)
    #   index 511: VA [0xffffffffc0000000, 0xffffffffffffffff)
    #              -> PA [0x80000000, 0xc0000000)
    # The identity map keeps the current low PC executable immediately after
    # "csrw satp" (QEMU re-translates the next fetch), until "jr" reaches the
    # high virtual address.  Both are 1 GiB leaves with flags V|R|W|X|A|D.
    .align PGSHIFT
    .global boot_page_table_sv39
boot_page_table_sv39:
    .zero 8 * 2
    .quad (0x80000 << 10) | 0xcf            # index 2
    .zero 8 * (511 - 3)
    .quad (0x80000 << 10) | 0xcf            # index 511
