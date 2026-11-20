# Lab4 进程管理 实验报告

## 0. 基本信息与分工

- 实验：riscv64-ucore Lab4「进程管理」
- 小组成员：**2411520、2412267、2414067**
- 分工与代码注释对应关系：

| 学号 | 负责模块 | 代码位置 |
|---|---|---|
| 2414067 | 功能模块一：虚拟内存页操作 | `kern/mm/pmm.c`（`page_insert` / `page_remove` / `page_remove_pte`） |
| 2412267 | 功能模块二：进程创建及初始化 | `kern/process/proc.c`（`alloc_proc` / `proc_init` / `kernel_thread` / `do_fork` / `copy_thread` / `copy_mm`） |
| 2411520 | 功能模块三：进程调度与切换 | `kern/process/proc.c`（`cpu_idle` / `proc_run`）、`kern/schedule/sched.c`（`schedule`）、`kern/process/switch.S`（`switch_to`） |

代码中所有 `//LAB4 functionX YOUR CODE` 注释已按上表替换为对应学号。

运行环境：Ubuntu 26.04、QEMU 10.2.1、OpenSBI 1.8.1、`riscv64-unknown-elf` GCC 15.1.0（含 binutils/GDB 16.3.90）。

---

## 1. 实验目的

1. 理解虚拟内存管理的组织结构：多级页表、页表项格式、页表项的建立与撤销，以及引用计数和 TLB 的同步。
2. 理解进程/线程控制块（PCB）的字段含义，掌握内核线程的创建、首次运行现场的构造和登记流程。
3. 理解进程上下文切换与调度的关系：调度器选择线程，`proc_run` 切换页表，`switch_to` 保存/恢复寄存器现场。
4. 理解内核线程与用户进程的区别：内核线程只在内核态运行并共享内核地址空间；用户进程有独立地址空间，留待 lab5。

---

## 2. 整体逻辑线

本实验把系统从「单一执行流的内核」推进到「能够创建、登记、调度、切换多个内核线程」，整体顺序可以概括为：

```
kern_init
  ├─ pmm_init           物理内存管理 + 启动页表 + check_alloc_page / check_pgdir / check_boot_pgdir
  ├─ pic_init / idt_init
  ├─ vmm_init           VMA 管理的检查（mm_struct / vma_struct）
  ├─ proc_init          idleproc（第 0 个内核线程）+ initproc（第 1 个）
  ├─ clock_init / intr_enable
  └─ cpu_idle           空闲线程主体
         └─ schedule     从 proc_list 选择下一个 PROC_RUNNABLE
              └─ proc_run 更新 current、切 satp、flush TLB
                   └─ switch_to 保存旧 context、恢复新 context
                        └─ forkret → forkrets → __trapret
                             └─ kernel_thread_entry → init_main / 测试线程
                                  └─ 返回后 do_exit（lab4 中为 panic）
```

可以把它看成四个层次：

1. **物理页 → 页表**：`pmm_init` 建立以 4 KiB 页为单位的物理内存管理，并准备好启动页表；`page_insert`/`page_remove` 提供在页表上「挂页」和「摘页」的能力。
2. **页表 → 地址空间**：`vmm.c` 用 `mm_struct`/`vma_struct` 描述虚拟地址空间；本实验只做结构检查和预映射，不涉及缺页异常。
3. **地址空间 → 执行实体**：`proc_struct` 把 PID、内核栈、页目录、上下文、中断帧、状态等组织成一个可被调度的实体；`proc_init` 先把当前执行现场包装成 idleproc，再通过 `kernel_thread` 创建 initproc。
4. **执行实体 → CPU**：`schedule` 选择下一个线程，`proc_run` 完成页表和 `current` 的切换，`switch_to` 完成最底层的寄存器现场切换。

---

## 3. 功能模块一：虚拟内存页操作（2414067）

### 3.1 设计要点

`get_pte(pgdir, la, create)` 已经封装了 Sv39 三级页表的查找：

- `PDX1(la)` 索引根页表（VPN[2]）；
- `PDE_ADDR(pte)` 取出下一级页表的 PPN，`KADDR(PA)` 转成内核虚拟地址后索引 `PDX0(la)`（VPN[1]）；
- 再取下一级表，`PTX(la)` 索引叶页表项（VPN[0]）；
- `create=1` 时若中间级表不存在则 `alloc_page`、清零并写入 `PTE_V`；`create=0` 时只查找。

因此 `page_insert`/`page_remove` 不需要自己重写走表逻辑，只需要修改 `get_pte` 返回的 8 字节叶 PTE。

### 3.2 实现说明

**`page_insert(pgdir, page, la, perm)`**：

1. `ptep = get_pte(pgdir, la, 1)`，失败返回 `-E_NO_MEM`，保证不留下半完成映射；
2. 先 `page_ref_inc(page)`；
3. 若原 PTE 有效：
   - 原页等于新页：`page_ref_dec(page)` 抵消，引用净不变，但仍按新的 `perm` 重写 PTE；
   - 原页不同：调用 `page_remove_pte`，旧页引用减一，归零则释放，清旧项并刷 TLB；
4. 写入 `pte_create(page2ppn(page), PTE_V | perm)`；
5. `tlb_invalidate(pgdir, la)`。

**`page_remove(pgdir, la)`**：`get_pte(pgdir, la, 0)`；为空直接返回（幂等），否则调用 `page_remove_pte`。

**`page_remove_pte(pgdir, la, ptep)`**：只有当 `*ptep & PTE_V` 时，取物理页、`page_ref_dec`、归零则 `free_page`、`*ptep = 0`、刷 TLB。

设计上最关键的是「页表项、引用计数、TLB 三者同步」，以及「先增加新页引用再处理旧页」，避免同一页重复映射时误释放。

### 3.3 验证

启动日志中：

```
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
```

`check_pgdir` 覆盖了空映射、同页重复映射、不同页覆盖、引用计数、重复删除；`check_boot_pgdir` 还通过低地址 0x100 的真实 `strcpy` 验证了 MMU 能走通新建的页表。评分脚本中的 `[Test PMM] PASS` 也通过。

---

## 4. 功能模块二：进程创建及初始化（2412267）

### 4.1 `alloc_proc`

`kmalloc` 一个 `struct proc_struct`，并把所有字段置为安全初值：

- `state = PROC_UNINIT`、`pid = -1`；
- `runs = 0`、`kstack = 0`、`need_resched = 0`；
- `parent = NULL`、`mm = NULL`、`tf = NULL`；
- `context` 清零；
- `pgdir = boot_pgdir_pa`（内核线程共享内核页表）；
- `flags = 0`、`name` 清零。

### 4.2 `proc_init` 与 idleproc

- 初始化 `proc_list` 和 `hash_list`；
- 为当前执行现场分配 PCB，得到 **idleproc**：`pid = 0`、`state = PROC_RUNNABLE`、`kstack = (uintptr_t)bootstack`、`need_resched = 1`、名字 `"idle"`；
- `current = idleproc`；
- 调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 **initproc**，再用 `find_proc` 找到它并命名为 `"init"`。

一个容易忽略的点：**idleproc 不挂入 `proc_list`**。这样 `get_pid()` 第一次扫描时链表为空，返回 1，正好满足测试对 `initproc->pid == 1` 的要求。

### 4.3 `kernel_thread`

在栈上构造一个临时 `trapframe`：

- `gpr.s0 = fn`、`gpr.s1 = arg`；
- `status = (sstatus | SPP | SPIE) & ~SIE`，保证 `sret` 返回 S 态（SPP=1）且恢复中断使能（SIE=SPIE=1）；
- `epc = kernel_thread_entry`；
- 调用 `do_fork(clone_flags | CLONE_VM, 0, &tf)`。

### 4.4 `do_fork` 的七个步骤

1. `alloc_proc()` 分配 PCB，设置 `parent = current`；
2. `setup_kstack()` 分配 2 个连续物理页作为内核栈；
3. `copy_mm()` 处理地址空间；
4. `copy_thread()` 设置中断帧和上下文；
5. 关中断下分配 `pid`、挂哈希表、挂 `proc_list`、`nr_process++`；
6. `wakeup_proc()` 置为 `PROC_RUNNABLE`；
7. 返回新线程 PID。

前 3 步失败要逐级回滚：`setup_kstack` 失败释放 PCB，`copy_mm` 失败释放内核栈，保证不泄漏资源。

### 4.5 `copy_thread` 与首次运行链路

```c
proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
*(proc->tf) = *tf;
proc->tf->gpr.a0 = 0;                 /* 子线程返回值 0 */
proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
proc->context.ra = (uintptr_t)forkret;
proc->context.sp = (uintptr_t)(proc->tf);
```

首次被调度时的完整链路：

```
switch_to 恢复 context.ra = forkret
  → forkret 调 forkrets(current->tf)
    → forkrets: sp = a0(tf)，跳 __trapret
      → __trapret 从 trapframe 恢复所有寄存器并 sret
        → epc = kernel_thread_entry，s0 = fn，s1 = arg
          → kernel_thread_entry: a0 = s1; jalr s0
            → fn(arg)
```

### 4.6 `copy_mm`

本实验只创建内核线程，`current->mm == NULL`，所有线程共享内核地址空间，因此 `copy_mm` 直接令 `proc->mm = NULL; return 0;`。真正的用户地址空间复制/共享留到 lab5。

### 4.7 验证

评分脚本的 `[Test Process Create] PASS` 检查了：

- `current == idleproc`、`idleproc->pid == 0`、状态、`kstack == bootstack`、`parent/mm/tf/runs/flags/context.ra`；
- `initproc->pid == 1`、`parent == idleproc`、`mm == NULL`、`context.ra/sp` 非零。

---

## 5. 功能模块三：进程调度与切换（2411520）

### 5.1 `cpu_idle`

死循环检查 `current->need_resched`，为真时调用 `schedule()`。lab4 的调度点主要就在这里，以及测试代码显式调用的 `schedule()`。

### 5.2 `schedule`

在关中断的临界区内：

1. `current->need_resched = 0`；
2. 当前不是 idleproc 时，从 `current->list_link` 的下一个节点开始找；是 idleproc 时从 `proc_list` 头开始（FIFO 轮转）；
3. 找到第一个 `PROC_RUNNABLE` 的线程 `next`；
4. 找不到就退回 `idleproc`；
5. `next->runs++`；
6. 若 `next != current`，调用 `proc_run(next)`。

### 5.3 `proc_run`

同样在关中断下：

```c
current = proc;
write_csr(satp, ((uint64_t)SATP_MODE_SV39 << 60) | (next->pgdir >> RISCV_PGSHIFT));
flush_tlb();
switch_to(&(prev->context), &(next->context));
```

`SATP_MODE_SV39 = 8` 放在 satp 高 4 位，低 44 位是根页表物理页号。`libs/riscv.h` 里的 `lsatp()` 只能表达 Sv32，不能用于 Sv39。

### 5.4 `switch_to`

`struct context` 只包含 `ra`、`sp`、`s0~s11`。`switch_to` 是普通函数调用，编译器会在调用点自动保存调用者保存寄存器，因此只需手工保存被调用者保存寄存器：

- 保存：`ra`(0)、`sp`(8)、`s0`(16) … `s11`(104)；
- 恢复：从 `to` 中按同样偏移读回；
- 最后 `ret` 跳到 `to->ra`：老线程回到 `switch_to` 的调用点，新线程第一次进入 `forkret`。

### 5.5 验证

`[Test Schedule] PASS`：测试创建两个内核线程，两者都在 `schedule()` 的轮转中运行过；正常启动还输出了：

```
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

随后 `init_main` 返回，`kernel_thread_entry` 调用 `do_exit`，在 lab4 中触发 `panic("process exit!!.")`，这是预期结果。

---

## 6. Spec 与 Prompt

本实验采用「先写 spec，再写 prompt，生成后人工审查」的流程。每个练习的 spec 都包含四个部分：目标、接口、约束、验收标准；prompt 则把 spec 改写成可以直接执行的工程要求，写明涉及的源文件、已有符号、数据结构、错误码和测试断言。下面按三个练习分别给出 spec 与 prompt。

### 6.1 练习一：虚拟内存页操作

**Spec**

- **目标**：在 Sv39 页表上建立/撤销 4 KiB 页映射，保证页表项、页引用计数、TLB 三者同步。
- **接口**：
  - `int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)`
  - `void page_remove(pde_t *pgdir, uintptr_t la)`
  - 内部辅助 `static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)`
- **约束**：
  - 页表查找复用 `get_pte(pgdir, la, create)`：`create=1` 建立映射，`create=0` 只查找；不自己重写 VPN[2]/VPN[1]/VPN[0] 的走表逻辑。
  - `page_insert` 成功返回 `0`，中间级页表分配失败返回 `-E_NO_MEM`，不能留下半完成映射。
  - 三种情况分别处理：空项直接建立；同一物理页重复映射时引用净不变，但按新的 `perm` 重写 PTE；映射到不同物理页时先撤销旧映射（旧页引用减一，归零释放）。
  - 先 `page_ref_inc` 新页，再处理旧页，避免同页重复映射时误释放。
  - 所有页表修改后都要 `tlb_invalidate`。
  - `page_remove` 必须幂等：目标地址无映射时安静返回。
- **验收**：`check_pgdir()`、`check_boot_pgdir()` 通过，`[Test PMM] PASS`。

**Prompt**

```text
在 riscv64-ucore Lab4 的 kern/mm/pmm.c 中实现虚拟内存页操作。
已有接口：get_pte、pte_create、pte2page、page_ref_inc/page_ref_dec、
free_page、tlb_invalidate、flush_tlb、PDE_ADDR/PTE_ADDR、PTE_V/R/W/X/U。
要求：
1. 实现 static void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)：
   仅当 *ptep & PTE_V 时处理；旧页 page_ref_dec，归零则 free_page；
   *ptep=0；tlb_invalidate。
2. 实现 void page_remove(pde_t *pgdir, uintptr_t la)：
   ptep=get_pte(pgdir,la,0)；ptep 为空直接返回；否则调用 page_remove_pte；必须幂等。
3. 实现 int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)：
   ptep=get_pte(pgdir,la,1)，失败返回 -E_NO_MEM；
   先 page_ref_inc(page)；
   若 *ptep 有效：同一页则 page_ref_dec 抵消；不同页则调用 page_remove_pte；
   写入 pte_create(page2ppn(page), PTE_V|perm)；
   tlb_invalidate；成功返回 0。
不要修改函数签名和测试文件。
```

### 6.2 练习二：进程创建及初始化

**Spec**

- **目标**：在现有 `proc.c` 骨架上实现内核线程的创建、登记和首次运行现场构造。
- **要求函数**：`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`。
- **数据结构**：`struct proc_struct`、`struct context`、`struct trapframe`、`proc_list`、`hash_list`、`idleproc`、`initproc`、`current`、`nr_process`。
- **约束**：
  - `alloc_proc` 必须把所有字段置于安全初值，`pgdir = boot_pgdir_pa`；
  - idleproc 复用 `bootstack`，`pid = 0`，`need_resched = 1`，**不挂入 `proc_list`**；
  - initproc 必须得到 `pid = 1`，`parent = idleproc`，`mm = NULL`；
  - `kernel_thread` 的 trapframe 要设置 `s0 = fn`、`s1 = arg`、`epc = kernel_thread_entry`、`status` 的 SPP/SPIE/SIE 位；
  - `do_fork` 按「PCB → 内核栈 → mm → trapframe → pid/链表 → 唤醒」顺序执行，失败逐级回滚；
  - `copy_thread` 把 trapframe 放在内核栈顶，`a0 = 0`，`context.ra = forkret`，`context.sp = tf`；
  - `copy_mm` 在 lab4 只做占位：`current->mm == NULL` 时返回 0。
- **验收**：`[Test Process Create] PASS`，正常输出两行 `To U:` 后 `do_exit` panic。

**Prompt**

```text
在 riscv64-ucore Lab4 的 kern/process/proc.c 中实现内核线程创建。
已有接口：kmalloc/kfree、alloc_pages/free_pages、page2kva/kva2page、
set_proc_name、find_proc、get_pid、setup_kstack/put_kstack、wakeup_proc、
list_init/list_add、kernel_thread_entry、forkret、forkrets、__trapret。
要求：
1. alloc_proc：kmalloc PCB，state=PROC_UNINIT，pid=-1，context/tf/parent/mm
   清零，pgdir=boot_pgdir_pa，flags=0，name 清零。
2. proc_init：初始化 proc_list/hash_list；把当前现场做成 idleproc（pid=0，
   state=PROC_RUNNABLE，kstack=bootstack，need_resched=1，名字 "idle"，不加入
   proc_list）；current=idleproc；kernel_thread(init_main,"Hello world!!",0)
   创建 initproc；find_proc 后命名为 "init"。
3. kernel_thread：栈上 trapframe 清零，s0=fn，s1=arg，
   status=(sstatus|SPP|SPIE)&~SIE，epc=kernel_thread_entry，
   调用 do_fork(clone_flags|CLONE_VM, 0, &tf)。
4. do_fork：alloc_proc -> parent=current -> setup_kstack -> copy_mm ->
   copy_thread -> 关中断下 get_pid/hash_proc/list_add/nr_process++ ->
   wakeup_proc -> 返回 pid；前三步失败要回滚。
5. copy_thread：tf 放在 kstack+KSTACKSIZE-sizeof(trapframe)，复制，
   gpr.a0=0，gpr.sp=(esp==0)?tf:esp，context.ra=forkret，context.sp=tf。
6. copy_mm：current->mm 为 NULL 时 proc->mm=NULL 并返回 0。
不要修改函数签名，不要修改测试文件，注意错误码使用 -E_NO_MEM/-E_NO_FREE_PROC。
```

### 6.3 练习三：进程调度与切换

**Spec**

- **目标**：实现 FIFO 轮转调度、进程切换和最小上下文切换。
- **要求函数**：`cpu_idle`、`schedule`、`proc_run`、`switch_to`。
- **约束**：
  - `schedule` 在关中断下清 `need_resched`，从 `proc_list` 选择第一个 `PROC_RUNNABLE`；当前不是 idle 时从当前节点之后开始，是 idle 时从链表头开始；找不到就退回 idleproc；`next->runs++`；
  - `proc_run` 关中断，更新 `current`，写 `satp = (8<<60)|(pgdir>>12)`，`flush_tlb()`，再 `switch_to`；
  - `switch_to` 只保存/恢复 `ra`、`sp`、`s0~s11`，按 `struct context` 偏移；
  - 不能用 `lsatp()`（Sv32 遗留）。
- **验收**：`[Test Schedule] PASS`，两个测试线程都能被轮转执行。

**Prompt**

```text
在 ucore RISC-V Lab4 中实现调度与上下文切换：
1. proc.c::cpu_idle：while(1) 检查 current->need_resched，为真调用 schedule。
2. sched.c::schedule：保存中断状态；current->need_resched=0；
   last=(current==idleproc)?&proc_list:&current->list_link；
   从 last 的下一个节点开始循环查找 PROC_RUNNABLE；
   找不到则 next=idleproc；next->runs++；next!=current 时 proc_run(next)；
   恢复中断状态。
3. proc.c::proc_run：保存中断状态；current=proc；
   write_csr(satp, (SATP_MODE_SV39<<60)|(proc->pgdir>>RISCV_PGSHIFT))；
   flush_tlb()；switch_to(&prev->context,&next->context)；恢复中断状态。
4. switch.S::switch_to：按 struct context 布局保存 a0 的 ra/sp/s0..s11，
   从 a1 恢复，最后 ret。
struct context 字段顺序：ra,sp,s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11。
```

### 6.4 Prompt 写法小结

- **Spec 四要素**：目标、接口签名、约束（状态机/引用计数/错误处理/原子性）、验收标准。
- **Prompt 要引用真实符号**：把骨架里已有的 `get_pte`、`boot_pgdir_pa`、`kernel_thread_entry`、`forkret`、`forkrets`、`__trapret`、`local_intr_save` 等写清楚，避免生成不存在的接口。
- **把测试断言写进 spec**：例如 idleproc 的 `kstack == bootstack`、initproc 的 `pid == 1`，这能显著减少返工。
- **明确边界**：不要修改函数签名和测试文件，错误码、返回路径、失败回滚都要在 prompt 里写清楚。

---

## 7. 调试过程中遇到的 Bug 与解决

### 7.1 骨架不完整导致的链接错误（预期）

最初 `make` 只能编译到链接阶段，未定义符号依次是 `page_insert`、`page_remove`、`proc_init`、`cpu_idle`，补完后还会出现 `schedule`、`switch_to`、`kernel_thread` 等。这是骨架要求补齐的部分，按模块逐个实现后解决。

### 7.2 QEMU 10.2 对非叶 PTE 保留位的严格检查（关键）

**现象**：`page_insert`/`page_remove` 写完后，`check_pgdir()` 通过，但 `check_boot_pgdir()` 中执行 `strcpy((void *)0x100, ...)` 时机器反复复位，串口不断重复输出：

```
DTB Init
HartID: ...
DTB Address: ...
```

**定位**：用 QEMU 的异常日志：

```
qemu-system-riscv64 ... -d int,guest_errors -D qemu_int.log
```

在日志中看到：

```
cause=0x0f store_page_fault, epc=0xffffffffc0203db0, tval=0x0000000000000100
```

说明页表映射看起来已经写入，但 MMU 拒绝访问 0x100。

**根因**：骨架的 `get_pte()` 在创建中间级页表项时使用 `pte_create(page2ppn(page), PTE_U | PTE_V)`。RISC-V 规范规定**非叶 PTE 的 D/A/U 是保留位，必须为 0**；老版本 QEMU 不检查，QEMU 10.2 会严格检查，因此第一次通过 `get_pte` 建立的映射真正被 MMU 访问时就触发页故障。

**解决**：

1. `get_pte()` 中两个中间级项改为只设置 `PTE_V`；
2. `check_pgdir()` 中 `assert(boot_pgdir_va[0] & PTE_U)` 改为 `assert(boot_pgdir_va[0] & PTE_V)`（中间级只验证存在性）；
3. 叶 PTE 的 `perm` 仍由调用者传入，该带 `PTE_U` 的仍然带。

修复后 `check_pgdir()`、`check_boot_pgdir()` 均通过。证据见 `evidence/qemu10-nonleaf-pte-fault.log`。

### 7.3 QEMU 10.2 与 `-device loader` 启动参数不兼容

`tools/grade.sh` 原来使用：

```
-device loader,file=bin/ucore.img,addr=0x80200000
```

在 QEMU 10.2 下 OpenSBI 的 `Domain0 Next Address` 变成 0，内核根本不执行。改为：

```
-kernel bin/ucore.img
```

后 `make grade` 才能正常启动（Makefile 的 `qemu`/`debug` 目标此前已做同样修改）。

### 7.4 GCC 15 把函数指针不兼容升级为错误

`make grade` 注入测试后编译失败：

```
error: passing argument 1 of 'kernel_thread' from incompatible pointer type
note: expected 'int (*)(void *)' but argument is of type 'void (*)(void *)'
```

原因是 `tools/test_lab4.c` 的 `test_lab4_thread` 声明为 `void (*)(void *)`，而 `kernel_thread` 的接口是 `int (*)(void *)`。GCC 14+ 将这类不兼容指针默认当作错误。将测试线程改为返回 `int`（函数本身是无限循环，语义不变）后编译通过。这是本地评分环境的适配，不改变测试逻辑。

### 7.5 `do_exit` 的 panic 不是 bug

`init_main` 打印完两行 `To U:` 后返回，`kernel_thread_entry` 会调用 `do_exit`。lab4 骨架中的 `do_exit` 仍是 `panic("process exit!!.")`，因此正常输出最后出现：

```
kernel panic at kern/process/proc.c:...
    process exit!!.
```

这是 lab4 的预期行为；`panic` 进入 `kmonitor`，`readline` 正好是 `grade.sh` 等待的断点，用来结束 QEMU 并收集串口输出。

---

## 8. 重要知识点与 OS 原理对照

| 实验中的实现 | OS 原理概念 | 含义、关系与差异 |
|---|---|---|
| Sv39 三级页表、`PDX1/PDX0/PTX` | 多级页表、地址翻译 | 用 9+9+9+12 位切分 39 位虚拟地址；实验直接操作页表项，原理课描述的是 MMU 的自动翻译过程。 |
| `PTE_V/R/W/X/U`、`pte_create` | 页表项权限、内存保护 | 实验通过 perm 控制页的读写执行权限；本实验的内核线程只用到内核页表，用户态保护要到 lab5 才完整体现。 |
| `tlb_invalidate` / `sfence.vma` | TLB、快表 | 修改页表后必须让 MMU 的缓存失效；原理课讲 TLB 命中/失效，实验用一条 `sfence.vma` 实现。 |
| `page_insert` 预映射 | 虚拟内存、按需分页 | 实验在建立映射时一次性分配物理页（预映射），不处理缺页；按需分页是「访问时才分配」，lab9 才涉及。 |
| `struct proc_struct` | 进程控制块 PCB | 实验的 PCB 包含 pid、state、kstack、pgdir、context、tf、父子/兄弟链表等；原理课的 PCB 还包含文件表、信号、记账等信息。 |
| `struct context` 只保存 `ra/sp/s0~s11` | 上下文切换、调用约定 | 利用 RISC-V ABI 的 callee-saved 寄存器约定，减少切换开销；原理课通常说「保存/恢复寄存器现场」，实验给出了最小集合。 |
| `trapframe` + `sret` | 中断/异常现场 | 实验用 trapframe 保存完整寄存器现场，`__trapret` 恢复后 `sret`；原理课的中断返回也是恢复 PC/状态字。 |
| `kernel_thread` / `do_fork` | 进程创建（fork/clone） | 实验通过复制当前现场并改写 trapframe 来「造」出新线程；原理课的 fork 还包括地址空间复制、COW、文件描述符继承。 |
| `schedule` FIFO 轮转 | 调度算法 | 实验实现最简单的 FIFO/轮转；原理课还有优先级、多级反馈队列、CFS、实时调度等。 |
| `local_intr_save/restore` | 临界区、原子性 | 实验用关中断保护 `current`/`proc_list`/`nr_process` 的修改；原理课用锁、信号量、原子指令等。 |
| `idleproc` | 空闲进程 | 没有其他可运行线程时占用 CPU；原理课中 idle 进程还承担省电（WFI）等职责。 |
| 内核线程共享 `boot_pgdir` | 地址空间隔离 | 内核线程共享内核地址空间；用户进程需要独立页表，lab5 才引入。 |

---

## 9. OS 原理中重要但本实验未覆盖的知识点

1. **用户态/内核态切换与系统调用**：本实验只创建内核线程，始终在 S 态运行；`ecall`、系统调用分发、用户栈/内核栈切换在 lab5。
2. **缺页异常与按需分页**：本实验是预映射，`do_pgfault` 只有声明没有实现；缺页处理、页面置换在 lab9。
3. **页面置换算法**：FIFO/LRU/Clock 等未涉及。
4. **同步互斥**：信号量、管程、条件变量在 lab7；本实验只用关中断保护临界区。
5. **文件系统与设备管理**：VFS、SFS、open/read/write 在 lab8。
6. **进程退出、等待与资源回收**：`do_exit` 仍是 panic，`do_wait`、父子进程回收未实现。
7. **写时复制（COW）、进程间通信、信号**：未涉及。
8. **多核调度与负载均衡**：实验是单 hart、单核视角。
9. **优先级/实时调度、公平性度量**：实验只有 FIFO 轮转。

---

## 10. 运行结果与评分

### 10.1 正常启动

`make qemu`（QEMU 10.2 + `-kernel bin/ucore.img`）关键输出：

```
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:...
    process exit!!.
```

### 10.2 官方评分

`make grade`：

```
-check PMM:                OK
-check Process Create:     OK
-check Schedule:           OK
Total Score: 30/30
```

日志保存在 `evidence/lab4-qemu.log`（正常启动）、`evidence/lab4-grade.log`（评分输出）和 `evidence/qemu10-nonleaf-pte-fault.log`（QEMU 10.2 页表故障复现）。

---

## 11. 环境适配说明

本实验在 QEMU 10.2 / GCC 15 下需要三处本地适配（均已在代码中注释或记录）：

1. **启动方式**：`tools/grade.sh` 的 `-device loader` 改为 `-kernel bin/ucore.img`；
2. **非叶 PTE 保留位**：`get_pte` 中间级只设 `PTE_V`，`check_pgdir` 的 U 位断言改为 V 位；
3. **GCC 15 函数指针检查**：评分测试的线程函数返回类型改为 `int`。

这些适配不改变实验要求的功能语义，只是让 lab2/lab3 遗留代码和评分脚本能在当前工具链上运行。

---

## 附录：关键代码位置

| 函数 | 文件:行 | 学号 |
|---|---|---|
| `page_remove_pte` / `page_remove` / `page_insert` | `kern/mm/pmm.c:264-329` | 2414067 |
| `alloc_proc` | `kern/process/proc.c:224` | 2412267 |
| `copy_mm` | `kern/process/proc.c:249` | 2412267 |
| `copy_thread` | `kern/process/proc.c:265` | 2412267 |
| `do_fork` | `kern/process/proc.c:284` | 2412267 |
| `kernel_thread` | `kern/process/proc.c:345` | 2412267 |
| `proc_init` | `kern/process/proc.c:362` | 2412267 |
| `cpu_idle` / `proc_run` | `kern/process/proc.c:404-435` | 2411520 |
| `schedule` | `kern/schedule/sched.c:15` | 2411520 |
| `switch_to` | `kern/process/switch.S` | 2411520 |
