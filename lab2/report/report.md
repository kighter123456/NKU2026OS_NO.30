# Lab2：物理内存管理、启动分页与 QEMU MMU 调试

日期：2026-10-25。本报告由 C 整合 A 的启动分页、B 的物理内存管理和 C 的集成调试结果。

**验收结论**：当前实现通过 35 个 PMM 自建场景、9 个分页启动 GDB 场景及 QEMU 源码双 GDB 检查。A 按指导书完成启动分页，但分工文档列出的通用 map/unmap/query API 和对应独立测试没有交付；B 的功能和自建测试已交付，课程原始 `basic_check/best_fit_check` 尚未取得。因此不能声称所有分工条目或课程评分均已通过。详细核查见 [A/B review](review-ab.md)。

## 1. 章节逻辑与项目组成

本章先解决“CPU 如何在高虚拟地址访问内核”，再解决“哪些物理页可以管理”，最后解决“如何把空闲页及页内对象分配出去”。启动分页负责 VA→PA 关系；物理页分配器负责页面所有权。分配一个页不会自动创建新的虚拟映射，本实验通过已经存在的内核直接映射访问分配结果。

实际执行顺序为：QEMU 装载镜像 → 复位 ROM → OpenSBI → 物理地址 `kern_entry` → 写 satp、执行 sfence.vma → 设置高地址栈并跳转 `kern_init` → 清 BSS → 解析 a1 指向的 DTB → `pmm_init` 选择并初始化管理器 → `page_init` 建立描述符并登记空闲区 → 分配器/SLUB 自测 → 空循环。

DTB 在本实现中先于 `pmm_init` 解析，随后 `page_init` 读取解析结果。这与指导书把探测描述在 PMM 初始化内部的写法略有不同，但依赖关系相同：没有有效内存范围就不能建立 Page 数组。原始 a0/a1 作为 C 参数传递，避免先写 BSS 全局变量、再被清零覆盖。

| 路径 | 核心职责 |
|---|---|
| `code/kern/init/entry.S`、`tools/kernel.ld` | 物理启动、高地址链接、启动页表、satp、栈 |
| `code/kern/init/init.c` | BSS、DTB、PMM、自测的初始化顺序 |
| `code/kern/mm/dtb.c` | 解析 FDT，取得连续 DRAM 范围 |
| `code/kern/mm/pmm.c`、`pmm.h`、`memlayout.h` | Page 描述符、地址换算、公共 PMM 契约 |
| `default_pmm.c`、`best_fit_pmm.c`、`buddy_pmm.c` | First-Fit、Best-Fit、Buddy 页面分配 |
| `code/kern/mm/slub.c`、`selftest.c` | 对象分配和独立状态检查 |
| `code/tools/boot.gdb`、`verify.py`、`integration.py` | 分页启动及整体验证 |
| `code/tools/qemu_host.py`、`qemu_mmu_debug.py` | 宿主 QEMU 源码与客户机内核双重调试 |

## 2. 分页模块的理解

内核链接地址为 `0xffffffffc0200000`，物理装载地址为 `0x80200000`，偏移量 `KERNEL_VA_PA_OFFSET=0xffffffff40000000`。汇编先用启动页表的链接地址减去偏移得到根页表 PA，再将其右移 12 位填入 satp.PPN；MODE=8 表示 RV64 Sv39。satp 保存物理页号，不能直接填虚拟指针。

启动页表根项 2 和 511 均为 `0x200000cf`，PPN=0x80000，标志为 V/R/W/X/A/D，U=0。根项直接含有 R/W/X，因此是第 2 级的 1 GiB 叶子，而非下一层页表指针。高地址段映射内核及 DRAM；低地址恒等映射保证写 satp 后、跳到高地址之前的取指仍有效。该映射不是第二张内核页表，也没有在本实验中撤销。

`sfence.vma` 使本 hart 不再使用旧的地址翻译缓存。修改页表内存与刷新翻译缓存是两个操作；实际多核系统还需其他 hart 配合失效。本实验没有多核 shootdown。页表预置 A/D=1，当前测试没有实际触发 QEMU 对 A/D 位的原子更新分支。

本次 GDB 检查了两个有效根项、起止边界、内核入口、DTB 高地址以及无效区域的查询，确认映射和标志符合预期。这是对既有启动映射的只读检查，不能算作 map/unmap 动态接口测试。[A 原始说明](report-a.md)保留其环境和实现记录；其中旧 Bare 日志是历史文本，当前状态以本报告和 C 日志为准。

## 3. 物理内存初始化与地址契约

`dtb_parse` 校验 FDT 头、长度、结构/字符串块边界、保留表终止项和 token，按父节点的 cells 解码 memory 节点。支持 1/2-cell 字段及 QEMU 的单段连续 DRAM；无效信息失败，不以固定 128 MiB 冒充探测结果。框架假定 OpenSBI 提供可读的 DTB 地址，不支持任意硬件保留区或多段 RAM 的通用管理。

`page_init` 将管理终点设为实际 RAM 末端与 `PHYSTOP=0x88000000` 的较小值并向下页对齐。描述符从链接符号 end 后的整页开始，描述符数组自身也必须从空闲区排除。所有 Page 先标记 reserved，只有完整可用页被交给 `init_memmap`。没有完整页时不能调用该接口。

```text
nbase = 0x80000000 / 4096 = 0x80000
npage = managed_end / 4096                 # PFN 上界，不是页数
Page 数量 = npage - nbase
page2pa(pages+i) = (nbase+i)*4096
KVA = PA + 0xffffffff40000000
free_begin = ROUNDUP(descriptor_end,4096)
```

当前 Page 为 56 字节，Page 指针指向元数据，不是物理页中的用户数据，也不是 PTE。`page2pa/pa2page` 在元数据与物理页之间换算；`KADDR/PADDR` 在已管理范围内换算 KVA/PA，并检查边界。DTB 在 256 MiB 配置下位于管理区外，`dtb_init` 使用覆盖 1 GiB 的启动映射访问它，不能误用只允许受管页的 KADDR。

本次 Best-Fit/128 MiB 的描述符 PA 区间为 `[0x80251000,0x80411000)`，空闲区 `[0x80411000,0x88000000)`，共 31727 页。这些数值受编译器和内核大小影响，测试按符号/运行结果计算，不作为通用常量。

公共接口规定 `alloc_pages(0)` 属非法调用；合法请求资源不足返回 NULL。First/Best-Fit 允许分批释放；Buddy 必须传回原始分配头及原始请求长度。`allocation_pages` 返回完整分配的实际跨度，Buddy 请求 5 页时是 8 页，不能按 5 页错误记账。

## 4. 分配模块与练习问题

### First-Fit 的初始化、分配、释放及改进

`default_init` 建立空链表和计数；`default_init_memmap` 把可用 Page 转为空闲块，块头记录 property 并按地址挂链。`default_alloc_pages` 选择第一个足够大的块，拆出 n 页，剩余部分重新形成空闲块，计数扣 n。`default_free_pages` 先验证整个区间的所有权，再按地址插回，与前后物理连续的空闲块合并。

地址有序用于判断能否合并，不等于按大小排序。改进可以用 next-fit 游标减少重复扫描，或用分桶/平衡树降低查找成本；这些会增加元数据和维护复杂度。及时合并可以减轻外部碎片，但不能消除所有碎片。

### Best-Fit 的实现、注册及改进

Best-Fit 遍历所有能满足请求的空闲块，选择最小块，同大小选低地址；拆分和合并仍维护地址顺序。`best_fit_pmm_manager` 注册进公共管理器，默认 `PMM=best_fit`，因此启动的不是仅改名的 First-Fit。

测试隔离低地址 8 页与高地址 4 页，申请 3 页，检查 First-Fit 选前者、Best-Fit 选后者。进一步可同时维护大小索引和地址索引，以降低搜索成本；但最小余块可能造成更多小碎片，不能认定 Best-Fit 在所有工作负载上更好。本次未测出性能排名。任务提示词见 [P3](prompt.md#p3best-fit-实现)。课程原始 `basic_check/best_fit_check` 缺失，当前自建测试无法证明兼容原框架的全部隐含状态约定。

### Buddy：实现与独立测试

Buddy 按 PFN 对齐把任意连续空闲区拆为最大合法二次幂块，各阶分别维护链表。申请时向上取整并逐级拆分；释放时用 PFN XOR 计算伙伴，仅合并同阶、空闲、范围合法的伙伴。请求长度和实际块大小分别记录，用于防止错误释放并核对空闲计数。代价是内部碎片，例如申请 5 页占用 8 页。

实现提示词和测试提示词分别见 [P4](prompt.md#p4buddy-实现)。测试覆盖非二次幂、各阶及 ±1 边界、拆分合并、非伙伴、耗尽、完整回收、错误长度/内部页/重复释放。隔离 8 页、占满其他区域，可防止从其他空闲块成功分配掩盖拆合错误；独立位图、物理对齐、数据标签及逐步计数能交叉发现重叠和记账问题。

### 简化 SLUB：两层分配及独立测试

PMM 提供页，SLUB 把页切分为 16～2048 字节规格对象，使用 128 字节对齐头、空闲链和占用位图管理。大对象走连续页路径；对象至少 16 字节对齐，空页归还 PMM，内部指针和重复释放通过所有权及 bitmap 检查。它只体现页/对象两层结构，没有 Linux SLUB 的 per-CPU、NUMA、并发和完整回收策略。

实现和测试两份提示词见 [P5](prompt.md#p5简化-slub-实现)。覆盖规格边界、大对象、600 个对象跨页、交错释放/复用、空页归还和 OOM。写满每个请求的字节并检查其他存活对象，可检验重叠及布局；最后核对页数恢复，可检验泄漏。三种页分配后端均运行这组对象测试。

### Challenge：不知道硬件内存范围时如何获得

优先读取启动协议提供的 DTB、UEFI GetMemoryMap 或 x86 E820 等映射，再扣除固件和设备保留区；ACPI 可辅助描述拓扑。向未知地址写读探测可能触发 MMIO、副作用或总线错误，不能当作通用可靠方法。本次回答思考题，但没有实现跨硬件探测 Challenge。

## 5. C 集成、测试与调试

C 没有重新合并已合并的 PR：拉取 main 到 `52ae7d0` 后确认 [B PR #2](https://github.com/kighter123456/NKU2026OS_NO.30/pull/2) 和 [A PR #3](https://github.com/kighter123456/NKU2026OS_NO.30/pull/3) 均在提交历史中。保留两个模块的单一 Page 定义及地址契约，没有新增通用页表接口替代 A 的缺项。

集成修复包括：启动断点使用物理 `0x80200000`，高地址 C 入口再按符号定位；把仍显示 Bare 的日志及 runner 匹配更新为 Sv39；修正共享头里旧版 VM 编码；启动验证等到完整 PMM 自测成功；让 QEMU、容量和证据目录可配置。非法低地址测试现在期望先命中 VA 下界检查，未删改内核断言。

本轮环境是 WSL2 Ubuntu 22.04.1、GCC 10.2.0、binutils 2.35.1、Make 4.3、Python 3.10.6、系统 QEMU 6.2.0/OpenSBI 0.9、GDB 12.1。A/B 历史报告使用不同环境，未将它们的旧结果冒充为本轮结果。

| 检查 | 结果与证据 |
|---|---|
| PMM 功能回归 | 35/35：9 个正常容量/后端组合 + 26 个指定断言拒绝场景；[summary-all.json](../code/evidence/selftest/summary-all.json) |
| 分页启动 GDB | 9/9：三后端 × 64/128/256 MiB；[summary.json](../code/evidence/integration/summary.json) |
| 启动映射 | 核对 satp、根 PA、两个有效项、映射边界、DTB、无效区域、BSS、SBI 及描述符布局；[示例 GDB 日志](../code/evidence/integration/boot-best_fit-256M/gdb.log) |
| 分配/释放 | 策略差异、拆分/合并、1200 步随机分配及独立位图、SLUB 规格/数据隔离/OOM |
| QEMU 源码 | Bare/Sv39、逐语句查表、TLB miss/fill/hit、BSS 实际 sb 写访问；[专门记录](gdb-mmu.md) |
| 修改边界 | lab1 的 50 个跟踪文件内容未变；Windows CRLF 按 LF 基线校验；[submission-check.json](submission-check.json) |

复现时在 `lab2/code` 执行 `make grade-integration`，源码调试另运行 `bash tools/build-debug-qemu.sh` 和 `make grade-source`。[运行说明](../code/README.md)给出依赖和命令；[测试记录](testing.md)说明失败定位与证据目录。

## 6. GDB 分支任务

宿主原生 x86-64 GDB 调试带符号 QEMU，客户机 gdb-multiarch 调试 RISC-V 内核。宿主通过地址条件断点过滤 CPU 访问，并排除 `is_debug=true` 的调试器读内存请求。实际单步观察到：取指 VA `0xffffffffc0200032` → 根 PTE PA `0x80207ff8` → PTE `0x200000cf` → PA `0x80200032`；BSS 的 `sb a1,-1(a5)` 则写 VA `0xffffffffc0208010` → PA `0x80208010`。

调用路径、逐条分支、三层循环与大页提前结束、Bare 为什么也使用 QEMU 软件 TLB、软件 TLB 与真实硬件差异，以及真实日志截图，统一见 [gdb-mmu.md](gdb-mmu.md)。本次使用 QEMU 6.2.0 源码适配指导书的 4.1.1 流程，函数及行号以 6.2.0 为准，没有声称实际调试过 4.1.1。

## 7. 实验知识点与 OS 原理的对应

| 实验知识点 | OS 原理含义、关系和差异 |
|---|---|
| VA、PA、PFN、Page 指针 | 地址空间、物理帧和元数据是不同对象；数值换算不代表取得所有权，也不代表建立映射 |
| Sv39 根表及 satp | 多级页表把稀疏地址空间分层描述；本实验用根层大页，未建立每进程三级小页结构 |
| PTE 权限、页对齐 | 页表同时承担访问控制及地址翻译；当前是宽松内核启动映射，没有用户隔离或 W^X 策略 |
| sfence.vma、QEMU 软件 TLB | TLB 缓存翻译并需要失效；真实硬件缓存 PA/权限，QEMU 还要缓存宿主内存访问加速信息 |
| DTB 与 Page 数组 | 物理资源发现和资源记账；不是通过虚拟内存大小推断 RAM，元数据也占物理页 |
| First/Best-Fit、Buddy | 连续资源分配与外部/内部碎片；算法策略、搜索开销和碎片之间存在取舍 |
| 简化 SLUB | 页分配上叠加对象分配，减少小对象浪费；没有实现完整生产内核的并发性能机制 |
| 断言、独立位图及数据标签 | 所有权、不重叠、计数等内核不变量；有限场景验证不等于形式化证明 |

重要但本实验未覆盖的原理包括：进程独立地址空间、用户态隔离、系统调用整体设计、缺页异常与按需分页、页面置换及交换、COW、共享内存、多核页表同步/锁、NUMA、调度和文件系统。当前也没有动态映射/撤销映射的接口及其失败回滚测试。

## 8. AI 协作与整合总结

三部分记录分别保留：[A 原始记录](report-a.md#4-ai-协作记录)、[B 预先设计的提示词](prompt.md)、[C 与汇总记录](ai-collaboration.md)。A/B 的人工决策引用其既有报告；本轮 C 的代码、调试和文档由 AI 根据用户分工执行，未虚构成员已经逐行人工复核或手工取得截图。

整体执行流已经贯通，C 的交付集中在可复现验证、实际 QEMU 源码现场、报告及接口说明。仍需小组确认 A 的通用接口是否属于最终交付范围；课程原测试取得后应原样接入。报告保留这两项明确边界，不把通过自建测试等同于课程验收完成。
