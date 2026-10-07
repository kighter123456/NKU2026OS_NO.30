# lab1：最小可执行内核与启动流程

实验日期：2026-10-07。基于文件夹中的原始 uCore 环境，按 [lab0 环境配置](lab2026-book/lab0/3_startdash.html)、[lab1 练习](lab2026-book/lab1/lab1_2_1_exercise.html)及[报告要求](lab2026-book/lab1/lab1_5_requirement.html)完成。本文的地址和寄存器数值来自实际编译、运行与调试，不是照抄指导书示例。

## 1. 从 lab0 开始配置环境

宿主机为 Windows，使用已有 Ubuntu WSL2 作为 Linux 开发环境，源码位置对应 `/mnt/e/桌面/OSLab/lab1`。交叉编译器在 x86_64 上执行，生成 RV64 机器码；QEMU 模拟 RISC-V CPU、内存、串口及固件。

| 工具 | 实际版本 | 用途 |
| --- | --- | --- |
| Ubuntu | 22.04.1 LTS，WSL2 | Linux 编译与调试环境 |
| GNU Make | 4.3 | 管理编译、链接和运行规则 |
| RISC-V GCC | 10.2.0 | 编译 C 与汇编 |
| RISC-V binutils | 2.35.1 | 链接、反汇编及镜像转换 |
| QEMU | 6.2.0 | 模拟 `virt` 机器 |
| GDB multiarch | 12.1 | 读取 RV64 指令及远程调试 |
| OpenSBI | 0.9，运行时 SBI 0.2 | 初始化机器态环境并提供 SBI 服务 |

安装软件包 `gcc-riscv64-unknown-elf`、`binutils-riscv64-unknown-elf`、`qemu-system-misc` 和 `gdb-multiarch`，沿用已有 Make 与 Python。发行版软件包安装后即可通过 `/usr/bin` 使用，无需额外修改 `.bashrc`。指导书关于 apt 中 QEMU 版本过低的说明针对旧环境，本机 6.2 已达到最低版本要求。

已实际检查 Linux 目录与源码、工具路径、版本和编译结果。完整版本输出见 [environment.txt](evidence/environment.txt)。源码通过 `make -B -j2` 全量编译，记录见 [build.log](evidence/build.log)。

## 2. 本章的整体逻辑

本章围绕“没有现成操作系统时，怎样运行第一个内核程序并输出信息”逐步展开：先建立交叉编译环境，理解 ELF 与原始镜像及内存段，再用链接脚本确定入口和布局，准备汇编入口与内核栈，进入 C 初始化函数，最后通过 SBI 服务完成格式化输出，并用 GDB 验证各阶段。

编译流程为：

```text
C / 汇编源码 -> RISC-V .o -> 按 kernel.ld 链接为 bin/kernel
             -> objcopy 转换为 bin/ucore.img -> QEMU 加载与执行
```

实际启动流程为：

```text
QEMU 预先加载 OpenSBI、内核镜像和设备树
CPU 复位到 0x1000（MROM）
  -> 0x80000000（OpenSBI，M 模式）
  -> 0x80200000（kern_entry，S 模式）
  -> 设置 sp -> kern_init -> 清零 BSS -> cprintf
  -> SBI ecall -> OpenSBI 串口输出 -> 返回 S 模式
  -> 内核无限循环
```

这里必须区分“加载”和“交接控制权”。本实验中 QEMU 在 CPU 执行第一条指令之前已将内核载入内存，OpenSBI 负责初始化及随后交接。启动时并不存在 OpenSBI 从磁盘读取本实验镜像的步骤，也没有模拟内核磁盘。

## 3. 练习 1：理解程序入口操作

### 3.1 `la sp, bootstacktop` 的操作和目的

`la` 是加载符号地址的汇编伪指令，将 `bootstacktop` 的地址写入栈指针寄存器 `sp`，不是读取该地址中的数据。启动栈由 `entry.S` 的 `.space KSTACKSIZE` 预留，`KSTACKSIZE = 2 * 4096 = 8192` 字节，底部按 4096 字节对齐。

栈从高地址向低地址增长，所以空栈的初始 `sp` 指向预留区域的高端。C 函数的局部变量、保存的寄存器、返回地址及调用帧需要有效栈空间；不能沿用 OpenSBI 的私有栈。

本次符号和单步结果：

```text
bootstack    = 0x80201000
bootstacktop = 0x80203000
进入 kern_entry 时：sp = 0x80017ee0（固件使用的栈）
执行 la 后：         sp = 0x80203000（内核启动栈）
```

实际展开为：

```asm
0x80200000: auipc sp,0x3
0x80200004: addi  sp,sp,0    # GDB 显示为 mv sp,sp
```

`auipc` 使用该指令所在地址加上 `0x3000`，本次低位偏移为 0，因此得到 `0x80203000`。两条指令属于同一条源码伪指令。栈顶满足 RISC-V ABI 要求的 16 字节对齐。`bootstacktop` 是边界标签，不占用字节，所以它与后续 `SBI_CONSOLE_PUTCHAR` 数据符号地址相同是正常的。

### 3.2 `tail kern_init` 的操作和目的

`tail` 是尾调用伪指令，将控制权交给 `kern_init`，不设置新的返回地址 `ra`。通常可以展开为 PC 相对地址计算和 `jalr x0,...`；本次链接器进行了松弛优化，最终变为一条 16 位压缩跳转，反汇编显示为：

```asm
0x80200008: j 0x8020000a
```

其目的在于结束汇编入口阶段，进入 C 语言内核初始化。`kern_init` 声明为 `noreturn` 并最终无限循环，因此无需返回入口汇编，也无需额外建立返回链。

实际观察到跳转前后 `ra` 均为 `0x800078cc`，进入 `kern_init` 的第一条指令时 `sp` 仍为 `0x80203000`。随后 C 函数自身的序言才会继续调整栈指针。不能把 `tail` 理解成必然清零 `ra`；它保留寄存器中的旧值。

## 4. 练习 2：用 GDB 验证启动流程

### 4.1 调试方法

在项目目录使用两个 Ubuntu 终端，先执行 `make debug`，再执行 `make gdb`。QEMU 的 `-S` 让 CPU 在复位位置暂停，`-s` 在端口 1234 提供 GDB 服务。GDB 加载的是带调试符号的 `bin/kernel`，QEMU 加载的是原始镜像 `bin/ucore.img`。

主要命令如下；也可执行 `make grade` 自动复现断点、单步和寄存器检查，完整输出见 [gdb.log](evidence/gdb.log)。

```gdb
x/6i 0x1000
si 6
info registers pc a0 a1 a2
b *0x80200000
continue
x/8i $pc
si 3
info registers pc sp ra
```

### 4.2 加电后最初的指令及功能

在本次 QEMU `virt` 模型中，连接后 `pc = 0x1000`。此处是 QEMU 构造的 MROM 复位跳板，尚未进入位于 DRAM 的 OpenSBI 主体。

| 地址 | 实际指令 | 功能 |
| --- | --- | --- |
| `0x1000` | `auipc t0,0x0` | 取当前地址，令 `t0 = 0x1000` |
| `0x1004` | `addi a2,t0,40` | 令 `a2 = 0x1028`，传递固件动态信息结构地址 |
| `0x1008` | `csrr a0,mhartid` | 读取当前硬件线程 ID，本次为 0 |
| `0x100c` | `ld a1,32(t0)` | 从 `0x1020` 取设备树地址，本次为 `0x87000000` |
| `0x1010` | `ld t0,24(t0)` | 从 `0x1018` 取 OpenSBI 入口 `0x80000000` |
| `0x1014` | `jr t0` | 跳转到 OpenSBI |

所以最初几条指令主要准备固件参数并跳转，完整的硬件初始化由后面的 OpenSBI 完成；`0x1000` 处的跳板和 `0x80000000` 处的固件应分别理解。地址是本次模拟平台的配置，不代表所有 RISC-V 硬件统一使用该复位地址。

### 4.3 从 OpenSBI 到内核第一条指令

单步执行六条复位指令后，观测值为：

```text
pc = 0x80000000
a0 = 0
a1 = 0x87000000
a2 = 0x1028
```

随后在 `0x80200000` 设置断点并继续，避免单步执行大量固件代码。OpenSBI 输出：

```text
Domain0 Next Address : 0x0000000080200000
Domain0 Next Mode    : S-mode
```

断点命中 `kern_entry`，此时 `pc = mepc = 0x80200000`，`satp = 0`。说明控制权交给内核时尚未启用分页；单步三条机器指令后到达 `kern_init = 0x8020000a`。这完成了练习所要求的复位到内核入口跟踪。

### 4.4 对内核加载时机的验证

自动脚本在 `pc = 0x1000`、CPU 尚未执行指令时，读取内存 `0x80200000` 的前 16 字节，与 `bin/ucore.img` 的前 16 字节进行比较，结果一致。

因此指导书建议的 `watch *0x80200000` 在当前加载方式下不会观察到固件加载瞬间，因为加载发生在连接 GDB 之前。观察交接时应使用入口断点；若调试其他具有运行时搬运步骤的固件，写观察点才可能观察到搬运行为。

### 4.5 进一步验证 SBI 输出

沿 `cprintf -> sbi_console_putchar` 设置断点，在第一字符 `(` 的输出处记录：

```text
ecall 前：pc=0x80200492，a7=1，a0=0x28
机器态入口：pc=mtvec=0x80000520
mcause=9，mepc=0x80200492，mstatus.MPP=1
返回后：pc=0x80200496
```

`mcause=9` 对应 S 模式环境调用；`MPP=1` 记录陷入前的 S 模式。OpenSBI 处理字符后返回 `ecall` 后一条指令，控制台最终输出 `(THU.CST) os is loading ...`。实际 QEMU 输出见 [qemu.log](evidence/qemu.log)。

## 5. 核心函数与模块

| 模块 | 作用及关键约束 |
| --- | --- |
| `tools/kernel.ld` | 指定 RISC-V 架构、`kern_entry` 入口和 `0x80200000` 基址；排列代码、只读数据、数据与 BSS |
| `kern/init/entry.S` | 静态预留启动栈，设置 `sp`，以尾调用进入 C |
| `kern/init/init.c::kern_init` | `memset(edata,0,end-edata)` 清理零初始化区，打印信息，进入无限循环 |
| `libs/string.c::memset` | 按字节设置内存；内核使用自有实现，不依赖宿主系统 glibc |
| `kern/libs/stdio.c::cprintf` | 建立可变参数列表，调用 `vcprintf` 并返回打印字符数 |
| `libs/printfmt.c::vprintfmt` | 解析格式字符串，经回调输出字符 |
| `kern/driver/console.c::cons_putc` | 转交字符到 SBI 控制台接口 |
| `libs/sbi.c::sbi_call` | 按旧版 SBI 调用约定传递功能号、参数，并执行 `ecall` |
| `Makefile`、`tools/function.mk` | 管理依赖、编译链接、镜像生成及运行调试 |

`ALIGN(0x1000)` 的含义是对齐到 4096 字节边界。原始二进制镜像没有 ELF 头和调试符号，加载地址由 QEMU 参数决定。通常 `objcopy -O binary` 不将 `NOBITS` 类型的 BSS 物化为文件中的零字节，内核需要自己清零。ELF 的程序头描述加载段，节头描述链接和调试相关的节，二者不能混为一谈。

本次 ELF 的 `.text` 从 `0x80200000` 开始，`.data` 从 `0x80201000` 开始，`.sdata` 从 `0x80203000` 开始，入口为 `0x80200000`。详见 [elf-layout.txt](evidence/elf-layout.txt)及[symbols.txt](evidence/symbols.txt)。

当前链接后的 `edata = end = 0x80203008`，BSS 长度为 0：没有保留下来的非零长度未初始化全局区域。因此脚本检查了边界及空区间，但本次不能据此声称验证了非空 BSS 的写入清零效果。启动栈位于 `.data`，是原始镜像中的真实字节。

## 6. 实际问题与修改

### 6.1 新版 OpenSBI 的入口地址

原 Makefile 使用 `-device loader,file=bin/ucore.img,addr=0x80200000`。在当前 QEMU 6.2/OpenSBI 0.9 组合下，它加载了内核字节，却没有设置 OpenSBI 动态信息中的下一阶段地址，固件打印 `Domain0 Next Address : 0x0000000000000000`。GDB 能单步进入 OpenSBI，但无法命中内核入口，自动验证超时。

将 `qemu` 和 `debug` 目标改为 `-kernel bin/ucore.img` 后，QEMU 将 RV64 原始镜像载入 `0x80200000`，同时设置正确的下一阶段入口，成功启动。修复前的记录保存在 [qemu-before-fix.log](evidence/qemu-before-fix.log)和[gdb-before-fix.log](evidence/gdb-before-fix.log)，可与成功记录对照。

### 6.2 调试器与缺失的评分脚本

发行版提供多架构 GDB，而非指导书工具链中的同名前缀 GDB。Makefile 增加自动选择，并让 `gdb` 目标先确保 ELF 已编译。

原始 Makefile 引用的 `tools/grade.sh` 在基础文件中不存在。本次新增 `tools/verify.py` 与 `tools/boot.gdb`，通过 `make grade` 执行启动验证，使用临时本机调试端口并在结束后回收 QEMU。现在 `grade` 也读取编译依赖，避免修改头文件后沿用过期目标文件。

这是针对本实验行为的自建检查，不等同于课程官方评分。未改动已有内核启动逻辑，也未增加后续实验的页表、时钟中断或进程管理。

### 6.3 验证结果

| 检查 | 实际结果 |
| --- | --- |
| 全量编译链接及生成镜像 | 通过，无编译警告或错误 |
| 复位地址及最初六条指令 | 通过，`0x1000 -> 0x80000000` |
| OpenSBI 交接到内核 | 通过，`kern_entry = 0x80200000` |
| 8 KiB 栈、ABI 对齐、`sp` 初始化 | 通过 |
| 尾调用到 C 且保留 `ra` | 通过 |
| BSS 边界检查 | 通过，当前区间长度为 0 |
| 字符参数、SBI 陷入与返回 | 通过，`mcause=9`，返回 `ecall+4` |
| `make qemu` 的正常启动输出 | 通过，记录见 [run.log](evidence/run.log) |

内核打印后无限循环属于本章设计。运行验证在三秒后主动终止 QEMU，因此记录尾部的终止信息不是内核崩溃。

## 7. 实验知识点与 OS 原理的对应

| 实验知识点 | OS 原理中的含义 | 联系与本实验的边界 |
| --- | --- | --- |
| 固件与内核启动 | 建立操作系统运行条件并移交控制权 | 使用预置 OpenSBI，没有自行编写完整 bootloader |
| M/S 特权级及 SBI | 特权分离、受控陷入与返回 | 内核向机器态固件请求服务；不同于用户程序向内核发起系统调用 |
| 栈与调用约定 | 保存执行上下文、支持函数调用 | 此处只有启动栈，还没有每进程内核栈与上下文切换 |
| 段布局及链接 | 将代码和数据放入预定地址 | 布局为物理地址，尚无分页、虚拟地址空间和权限隔离 |
| BSS 初始化 | 为零初始化的全局对象建立运行时语义 | 裸机入口主动调用 `memset`；普通用户程序常由加载器和运行时处理 |
| 控制台 I/O | 设备访问的抽象与分层 | 借助 SBI 完成字符输出，没有实现完整终端或串口驱动 |
| 可执行文件与加载 | 从文件布局转为可执行内存布局 | ELF 用于链接和调试，原始镜像由 QEMU 预加载 |
| 远程调试 | 观察指令、状态和异常以定位错误 | GDB 借助模拟器观察整个客体，不依赖内核已提供调试服务 |

OS 原理中重要但本实验尚未实现的内容包括：物理内存分配和回收、页表与 TLB、虚拟内存及缺页处理、进程与线程、调度及上下文切换、用户态与系统调用、时钟中断、同步互斥、文件系统、持久化存储、多核并发及安全隔离。这些内容不能从“能启动和打印”推断为已经实现。

## 8. 证据与复现

下面按照证据、结论和执行路径组织记录，所有验证日志可通过项目中的脚本重新生成。

| Evidence | 原始来源 | 支持的 Finding | 复现方式 |
| --- | --- | --- | --- |
| E-001 | `evidence/build.log`、`evidence/elf-layout.txt` | F-001：生成 RV64 ELF，入口及布局正确 | `make -B -j2`；`riscv64-unknown-elf-readelf -h -S bin/kernel` |
| E-002 | `evidence/gdb.log` | F-002：从 MROM 经 OpenSBI 到 C 入口，栈和跳转符合预期 | `make grade` |
| E-003 | `evidence/qemu.log`、`evidence/run.log` | F-003：SBI 调用及控制台输出实际完成 | `make grade`；`make qemu` |
| E-004 | `evidence/qemu-before-fix.log` | F-004：旧参数在当前版本将下一阶段入口设为 0 | 成功日志对比旧日志；复现旧启动参数时需要限时停止 QEMU |

Path P-001：配置 Linux 工具（版本记录）-> 编译与检查布局（E-001/F-001）-> 复位单步、入口断点及栈检查（E-002/F-002）-> SBI 陷入和串口输出（E-003/F-003）。兼容性诊断 E-004/F-004 解释了启动参数调整的必要性。

最短复现命令，在 PowerShell 执行：

```powershell
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1" --exec make grade
```

## 9. AI 协作与提交状态

本次按 lab0.5 的“理解任务、梳理需求、生成、编译测试、根据证据迭代”流程使用 AI 协助完成。需求限定为 lab1 的两个练习及启动验证，首先检查指导书与真实源码，再运行编译和调试，依据失败日志修复 QEMU 参数，最后以成功日志编写本报告。

报告及验证脚本由 AI 辅助生成；报告中的调试过程由工具实际执行。提交前需要实验参与者理解上述指令和观察结论，并按课程要求补充小组身份信息。本地实验、记录和报告均已完成，代码和报告使用小组仓库 [NKU2026OS_NO.30](https://github.com/kighter123456/NKU2026OS_NO.30) 管理；编译产物可通过 `make` 重新生成，不纳入 Git。
