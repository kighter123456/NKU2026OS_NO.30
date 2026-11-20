#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <vmm.h>
#include <sbi.h>

#define TICK_NUM 100
/* 原始包与指导书未给出数值：沿用 lab3 累计输出 10 行后关机的约定。 */
#define SHUTDOWN_AFTER 10

static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr)
{
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

extern struct mm_struct *check_mm_struct;

//lab3 请补充你在lab3中的代码
void interrupt_handler(struct trapframe *tf)
{
    /* 静态变量跨中断保留，启动时由 BSS 清零。
     * printed_lines 记录实际统计行数，不能用 ticks 的绝对值替代。
     * shutdown_requested 防止关机接口意外返回后重复请求关机。 */
    static size_t printed_lines;
    static bool shutdown_requested;

    /* RV64 的 scause/frame.cause 是 64 位：
     *   bit63：中断标志（1=中断，0=同步异常）；bit62～0：具体原因码。
     * 先左移再右移去掉 bit63，才能和 IRQ_S_TIMER=5 等原因常量比较。 */
    uintptr_t cause = (tf->cause << 1) >> 1;
    const char *type;

    switch (cause)
    {
    case IRQ_S_TIMER:
        /* SBI 只预约一次事件，必须先安排下次事件，才能持续产生中断。 */
        clock_set_next_event();
        ++ticks;
        if (ticks % TICK_NUM == 0)
        {
            print_ticks();
            ++printed_lines;
            if (printed_lines >= SHUTDOWN_AFTER && !shutdown_requested)
            {
                shutdown_requested = 1;
                sbi_shutdown();
            }
        }
        return;
    case IRQ_U_SOFT:  type = "User software"; break;
    case IRQ_S_SOFT:  type = "Supervisor software"; break;
    case IRQ_H_SOFT:  type = "Hypervisor software"; break;
    case IRQ_M_SOFT:  type = "Machine software"; break;
    case IRQ_U_TIMER: type = "User timer"; break;
    case IRQ_H_TIMER: type = "Hypervisor timer"; break;
    case IRQ_M_TIMER: type = "Machine timer"; break;
    case IRQ_U_EXT:   type = "User external"; break;
    case IRQ_S_EXT:   type = "Supervisor external"; break;
    case IRQ_H_EXT:   type = "Hypervisor external"; break;
    case IRQ_M_EXT:   type = "Machine external"; break;
    case IRQ_COP:     type = "Coprocessor"; break;
    case IRQ_HOST:    type = "Host"; break;
    default:
        /* 先报告原因码，再按原框架输出现场；不修改被保存的状态。 */
        cprintf("Unknown interrupt: cause %lu\n", (unsigned long)cause);
        print_trapframe(tf);
        return;
    }
    cprintf("%s interrupt (cause=%lu)\n", type, (unsigned long)cause);
}

void exception_handler(struct trapframe *tf)
{
    /* 同步异常没有最高位的中断标志，直接用 cause 区分原因。 */
    const char *type;
    switch (tf->cause)
    {
    case CAUSE_ILLEGAL_INSTRUCTION:
        type = "Illegal instruction";
        break;
    case CAUSE_BREAKPOINT:
        type = "Breakpoint";
        break;
    default:
        /* 其他异常只诊断，不假设“跳过一条指令”就能恢复。 */
        print_trapframe(tf);
        return;
    }

    /* 先打印故障指令的原地址；推进后的 epc 是恢复地址，不是故障位置。 */
    cprintf("%s at 0x%016lx\n", type, (unsigned long)tf->epc);
    /* 低两位非 11 为 16 位压缩编码，11 为 32 位编码。 */
    uint16_t instruction = *(const volatile uint16_t *)tf->epc;
    /* 只修改 frame 的 epc；RESTORE_ALL 会把它写回 sepc，避免重复陷入。 */
    tf->epc += (instruction & 3) == 3 ? 4 : 2;
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
    if ((intptr_t)tf->cause < 0)
    {
        // interrupts
        interrupt_handler(tf);
    }
    else
    {
        // exceptions
        exception_handler(tf);
    }
}
