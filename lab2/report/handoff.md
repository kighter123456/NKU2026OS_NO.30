# Lab2 当前状态与 C 交接

> 2026-10-25 C 更新：本文保留交接时的历史契约。当前 C 的集成验证、源码双 GDB 和报告已交付；最新结论见 [整合报告](report.md)、[核查](review-ab.md)、[测试](testing.md)和 [QEMU 调试](gdb-mmu.md)。下文“待 C 完成”及 fork/PR 建议描述历史阶段，不代表维护者需要重复创建 PR。

日期：2026-10-25。B 的物理内存管理已合并（PR #2）；A 已按指导书接入启动分页（最小范围）。本文给 C 做 QEMU 源码双重调试和最终集成。

## 1. 当前状态

启动顺序：

```text
entry.S 启动页表 → satp Sv39 → 高地址 sp / kern_init
  → 清 BSS → 解析 DTB → pmm_init → B 自测 → 空循环
```

- 内核链接地址 `0xffffffffc0200000`，物理装载 `0x80200000`，虚实偏移 `0xffffffff40000000`（`KERNEL_VA_PA_OFFSET`）。
- `entry.S` 的 `boot_page_table_sv39` 有两个 1 GiB 叶子：
  - index 511：`0xffffffffc0000000..` 映射到 `0x80000000..`；
  - index 2：`0x80000000..` 恒等映射。QEMU 10.2 在 `csrw satp` 后立即用新页表翻译下一条取指，只映射高地址会在 `0x80200020` 取指缺页（`scause=12`），异常跳回 `stvec=0x80200000` 形成死循环。该恒等映射只存在于启动页表。
- satp 的 MODE=8（Sv39），PPN 为启动页表物理页号。本机实测 `satp=0x8000000000080207`，根页 `0x80207000`。
- `pmm_init()` 末尾打印 `satp virtual address` / `satp physical address`。
- 内核一直使用 `entry.S` 的启动页表；没有用 PMM 再建第二张内核页表，也没有通用 `map_page/unmap_page/query_page` 和 A 的映射测试。指导书这一节只要求把启动页表跑起来。
- B 的 PMM 自测在 Sv39 下运行并通过；Best-Fit / First-Fit / Buddy 的 `make grade` 均 PASS。
- 旧日志和文档仍写 `(Bare standalone)`，C 需要统一更新（见 §5.5）。

## 2. B 已提供接口

| 接口 | 约定 |
|---|---|
| `kern_init(hartid,dtb_pa)` | `entry.S` 的 a0/a1 原样传入；先清 BSS，再解析 DTB |
| `dtb_init` / `get_memory_base/size` | 复制连续 RAM 范围；不保留 DTB 内容指针 |
| `pmm_init` | 选择管理器、初始化、建立描述符和空闲区；不自动执行测试 |
| `alloc_pages(n)` | n>0；失败 NULL；返回 Page 描述符，不是数据地址 |
| `free_pages(base,n)` | Fit 支持分段释放；Buddy 只接受原始头和原始请求 n |
| `nr_free_pages()` | 单位为页；Buddy 扣除实际二次幂块大小 |
| `allocation_pages(head)` | 完整分配的实际跨度；原始头且整段仍已分配，不适用于部分释放后的分配 |
| `page2pa/pa2page` | `pages[0]` 对应 DRAM_BASE，不对应物理地址零 |
| `PADDR/KADDR/page2kva` | 集中转换；数据访问不要直接解引用物理地址数值 |
| `kmalloc/kfree` | 零大小返回 NULL，释放 NULL 无操作，最少 16 字节对齐 |
| `run_selftests` | 独立测试入口，集成后可单独控制 |

## 3. 公共结构与状态

Page 包含指导书的 ref、flags、property、page_link，另有 `alloc_span/requested` 记录实际跨度和请求长度，当前 `sizeof(Page)=56`。改变结构后须重新计算描述符占用、跑布局测试，不要硬编码空闲页数。

flags 按掩码使用：1=reserved，2=空闲块头，4=allocated，8=SLUB 所有权。与使用位序编号的框架不能直接混用。SLUB 所有权只标记分配头；归还物理页时清除。

Fit 空闲头的 property 为页数；Buddy 空闲头的 property 为阶，order=0 合法，必须同时判断 `PageProperty`。非头页不挂空闲链表。当前单 hart、无锁，不提供 SMP 保证。

`riscv.h` 存在历史 `VM_SV39=9` 宏；现代 RV64 satp 的 Sv39 MODE 是 8。A 没有使用或修改该旧宏。

## 4. A 的分页改动（已完成）

| 文件 | 改动 |
|---|---|
| `tools/kernel.ld` | `BASE_ADDRESS = 0xFFFFFFFFC0200000` |
| `kern/init/entry.S` | 启动页表、satp、`sfence.vma`、高地址 sp 和跳转 |
| `kern/mm/memlayout.h` | `KERNEL_VA_PA_OFFSET = 0xffffffff40000000` |
| `kern/mm/pmm.c` | `pmm_init()` 打印启动页表虚实地址 |

证据在 `../code/evidence/paging/`，说明见 [report-a.md](report-a.md)。

对现有测试的影响：

- `tools/boot.gdb` 仍断言 `kern_entry` 在 `0x80200000`、`sp=bootstacktop`（物理）、satp 为 Bare；开启分页后这些断言不再成立。
- `tools/verify.py` 依赖 boot.gdb 的 `LAB1 BOOT CHECKS PASSED`。
- `tools/run_tests.py` 匹配 `(Bare standalone)`；该字符串来自 `pmm.c`，A 没有改。
- B 的 `report/report.md` 和 `code/README.md` 是 B 阶段的 Bare 描述；C 集成时需要加注或更新，否则会和分页后的实际行为矛盾。

## 5. C 的任务

指导书的“分支任务：gdb 调试页表查询过程”是 C 的核心任务；下面是按指导书拆开的执行项。

### 5.1 准备带调试信息的 QEMU

1. 指导书用 qemu-4.1.1。当前宿主是 Ubuntu 26.04 + GCC 15，老版本源码未必能直接编译；可以用能编译的新版 QEMU 源码，但必须在报告中记录实际版本，并说明源码路径/函数名与 4.1.1 不同。
2. 按所用版本的构建方式开启调试信息。指导书写的是：
   ```bash
   ./configure --target-list=riscv32-softmmu,riscv64-softmmu --enable-debug
   make -j$(nproc)
   ```
   新版 QEMU 使用 meson/ninja，配置项和产物路径不同（常见为 `build/qemu-system-riscv64`），以实际版本为准。
3. **不要执行 `make install`**。保留系统 QEMU 作为日常运行版本，调试版单独使用。
4. 让 `make debug` 使用调试版 QEMU：可以临时覆盖 `QEMU` 变量，或在 Makefile 中增加一个变量。不要把个人绝对路径硬编码进提交的 Makefile。
5. 确认 `make debug` 仍使用 `-kernel $(UCOREIMG) -s -S`。QEMU 10.2 的 `fw_dynamic` OpenSBI 不接受 `-device loader` 启动方式。

### 5.2 双重 GDB

- 终端 1：项目目录执行 `make debug`，QEMU 停在 `-S`。
- 终端 2：`pgrep -f qemu-system-riscv64` 找到 PID；`sudo gdb`，`attach <PID>`，执行 `handle SIGPIPE nostop noprint`，设置 QEMU 源码断点后 `continue`。这是宿主机 GDB，调试 QEMU 本身。
- 终端 3：项目目录执行 `make gdb`，这是 `riscv64-unknown-elf-gdb`，通过 QEMU 的 gdbstub 调试 ucore。
- 报告里要说明两个 GDB 的分工：宿主机 GDB 看 QEMU 的 C 代码，RISC-V GDB 看内核指令和寄存器。

### 5.3 观察一次地址翻译

1. 选一个确定会被访问的虚拟地址，例如 `kern_init` 的高地址，或某个全局变量的地址；也可以选一次已知的取指/数据访问。
2. 在 QEMU 的地址翻译关键函数入口设置**条件断点**，条件为传入地址等于目标地址，避免每个 TB 都停下来。
3. 观察顺序应为：先查 TLB；TLB 命中直接得到物理页帧；未命中再按 Sv39 逐级读页表，最后把新映射填入 TLB。
4. 单步页表遍历，解释三级索引、PTE 读取、叶子判断和物理地址拼接；对应指导书里的“这三个循环”“这两行是不是从页表取 PTE”等问题。
5. 对比一次 satp=Bare 的取指（复位/OpenSBI 早期）和一次 satp=Sv39 的取指，说明 QEMU 模拟的 TLB 与真实 CPU TLB 在逻辑上的差异。
6. 记录调试中实际遇到的细节（条件断点、SIGPIPE、断点位置、地址大小等）和 AI 协作过程。

### 5.4 报告要求

按指导书“调试要求”逐项回答：

- QEMU 中 VA→PA 的关键调用路径和路径上的关键分支；
- 单步页表翻译的操作流程；
- QEMU 源码中模拟 TLB 查找的 C 代码及其细节；
- QEMU 模拟 TLB 与真实 CPU TLB 的逻辑区别；
- 调试过程中的抓马细节和通过大模型解决的问题（当时的情景、思路、交互过程）。

### 5.5 集成验收

1. 更新 `tools/boot.gdb`：复位时 satp=0；`kern_entry` 物理地址 `0x80200000`；执行 `entry.S` 后 PC/SP 为高地址，satp MODE=8；旧的低地址栈断言必须调整。
2. 更新 `tools/verify.py` 的检查文本；确保 `make grade-boot` 在新分页行为下通过。
3. 统一 `(Bare standalone)` 字符串：改 `pmm.c` 的日志，或改 `tools/run_tests.py` 的匹配；同时更新 B 的 README/报告中的 Bare 描述，避免前后矛盾。
4. 跑 `make grade-all` 和 `make grade-boot`；保留 B 的所有权断言，不要为了通过而删断言。
5. 新增高地址映射、satp 模式、启动恒等映射的检查；说明 index 2 的恒等映射是 QEMU 10.2 适配，不是额外的内核页表。
6. 课程 `basic_check/best_fit_check` 仍未取得；取得后原样接入，不修改原测试迁就实现，也不要把自建测试结果说成课程评分。

## 6. 环境与命令

当前验证环境：

```text
Ubuntu 26.04.1
riscv64-unknown-elf-gcc 15.1.0
riscv64-unknown-elf-gdb 16.3.90
qemu-system-riscv64 10.2.1 + OpenSBI 1.8.1
```

常用命令：

```bash
make
make qemu
make debug
make gdb
make grade
make PMM=first_fit grade
make PMM=buddy grade
make grade-all
make grade-boot
```

`make qemu/debug` 使用 `-kernel $(UCOREIMG)`；QEMU 不会自动退出，Ctrl+A 松开后按 X。

## 7. 分支与交付

- 当前改动推到 `handwriter-X/NKU2026OS_NO.30` 的新分支，再向原仓库 `kighter123456/NKU2026OS_NO.30:main` 提交 Pull Request；提交信息用英文说明改动，远端状态以 PR 为准。
- C 的交付至少包括：调试版 QEMU 构建说明、双重 GDB 操作记录、地址翻译调用路径和 TLB 分析、更新后的 boot/grade 脚本，以及 AI 协作记录。
- B 的原始 PR 是 #2（dowser47:lab2-b-pmm）；A 的启动分页在本分支追加，C 的 QEMU 调试可在后续分支继续。
