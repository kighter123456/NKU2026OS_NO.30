# Lab2 分页机制实现说明

日期：2026-10-25。范围：指导书“实现分页机制”。本说明只覆盖启动分页、satp 和 QEMU 适配，不包含额外的通用页表 API。

## 1. 指导书要求的改动

| 文件 | 改动 |
|---|---|
| `tools/kernel.ld` | `BASE_ADDRESS = 0xFFFFFFFFC0200000` |
| `kern/init/entry.S` | 建立 `boot_page_table_sv39`，写 satp，`sfence.vma`，设置高地址 sp 并跳到 `kern_init` |
| `kern/mm/memlayout.h` | `KERNEL_VA_PA_OFFSET = 0xffffffff40000000` |
| `kern/mm/pmm.c` | `pmm_init()` 末尾打印 `satp virtual address` / `satp physical address` |

`entry.S` 的启动页表是一个 1 GiB 叶子，把
`0xffffffffc0000000..0xffffffffffffffff` 映射到
`0x80000000..0xbfffffff`。内核链接基址是 `0xffffffffc0200000`，
对应物理地址 `0x80200000`。

`pmm.c` 在 `page_init()` 之后打印启动页表地址。本机实测：

```text
satp virtual address: 0xffffffffc0207000
satp physical address: 0x0000000080207000
```

具体地址随内核大小变化，不应硬编码。

## 2. QEMU 10.2 的必要适配

指导书的启动页表只设置索引 511。直接照做后，本机 QEMU 10.2.1 上内核
静默死循环。GDB 现场：

```text
scause = 12 (instruction page fault)
sepc   = 0x80200020
stval  = 0x80200020
```

`0x80200020` 是 `csrw satp` 的下一条指令。QEMU 10.2 在写 satp 后立即用
新页表翻译下一条取指，而旧页表没有映射低地址，于是取指异常；此时
`stvec=0x80200000`，异常又跳回 `kern_entry`，形成死循环。

因此在启动页表里增加索引 2 的低地址恒等映射
`0x80000000..0xbfffffff -> 同址`，让 satp 切换后的下一条指令仍可执行，
直到 `jr` 跳到高地址。这是环境适配，不是额外的分页功能；启动页表最终
仍是指导书描述的 1 GiB 高地址映射。

## 3. 验证

启动输出（Best-Fit，128 MiB）：

```text
(THU.CST) os is loading ...

DTB: hart=0 pa=0x0000000087e00000 RAM=[0x0000000080000000,0x0000000088000000) size=128 MiB
memory management: best_fit_pmm_manager (Bare standalone)
MANAGED: [0x0000000080000000,0x0000000088000000) 128 MiB
LAYOUT: descriptors=[0x0000000080251000,0x0000000080411000) Page=56 bytes
FREE: [0x0000000080411000,0x0000000088000000) pages=31727
satp virtual address: 0xffffffffc0207000
satp physical address: 0x0000000080207000
...
LAB2 B SELFTESTS PASSED: best_fit_pmm_manager
```

其他检查：

| 项目 | 结果 |
|---|---|
| 128 MiB Best-Fit | 启动正常，PMM 自测通过 |
| 256 MiB Best-Fit | DTB 位于 `0x8fe00000`（受管 128 MiB 之外），仍能解析，自测通过 |
| First-Fit / Buddy 128 MiB | 启动正常，PMM 自测通过；三种后端 `make grade` 均 PASS |
| GDB | `pc=0xffffffffc0200032`, `sp=0xffffffffc0207000`, `satp=0x8000000000080207`（MODE=8, PPN=0x80207） |
| 启动页表 | PTE[2]=PTE[511]=`0x200000cf`（PPN=0x80000, V|R|W|X|A|D） |

证据：

```text
code/evidence/paging/boot.log
code/evidence/paging/boot-256M.log
code/evidence/paging/boot-first_fit.log
code/evidence/paging/boot-buddy.log
code/evidence/paging/gdb-boot.log
code/evidence/paging/summary.txt
```

## 4. AI 协作记录

这一节是我先把指导书里的文字逻辑讲清楚，AI 负责生成代码和跑验证。范围由我定，代码和调试由 AI 执行。

链接脚本按指导书把内核放到高地址，物理装载位置不变，虚实偏移也按指导书设；在 `entry.S` 里准备一张启动页表，用 1 GiB 大页把内核所在的高地址段映射到物理内存开头，satp 按 Sv39 填好，写完刷新一下 TLB；然后把 sp 换成高地址的栈，跳到高地址的 `kern_init`；PMM 初始化之后把启动页表的虚拟地址和物理地址打印出来，方便确认页表位置。排错上，如果按指导书跑不起来，先用 GDB 看 `scause/sepc/stval`，确认是不是取指缺页，再决定要不要补映射。范围只做启动分页和 satp 打印，通用的 map/unmap/query、第二张内核页表和映射测试先不加。

AI 负责把这些想法落到 `kernel.ld`、`entry.S`、`memlayout.h` 和 `pmm.c` 里，并编译、跑 QEMU、看 GDB。按指导书版本启动时内核没有输出，GDB 看到 `scause=12`，`sepc` 指向 `csrw satp` 的下一条，判断是切换后取指缺页；我确认后在启动页表里补了一个低地址恒等映射，再跑就正常了。后来我把范围收紧，AI 也把超出指导书的页表模块和测试删掉，重新编译验证。

提交前我再逐行看一下启动页表、satp 和偏移计算。
