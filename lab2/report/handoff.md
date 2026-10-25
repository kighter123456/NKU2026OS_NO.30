# Lab2 B → A/C 接口交接

负责人：2414067—窦思微。当前基础是 lab1 的 Bare 启动，用户已明确允许 B 补齐必要公共基础；不代表代替 A/C 完成分页集成。

## 已提供接口

| 接口 | 约定 |
|---|---|
| kern_init(hartid,dtb_pa) | entry.S 的 a0/a1 原样传入；先清 BSS，再解析 DTB |
| dtb_init / get_memory_base/size | 复制连续 RAM 范围；不保留 DTB 内容指针 |
| pmm_init | 选择管理器、初始化、建立描述符和空闲区；不自动执行测试 |
| alloc_pages(n) | n>0；失败 NULL；返回 Page 描述符，不是数据地址 |
| free_pages(base,n) | Fit 支持分段释放；Buddy 只接受原始头和原始请求 n |
| nr_free_pages() | 单位为页；Buddy 扣除实际二次幂块大小 |
| allocation_pages(head) | 完整分配的实际跨度；原始头且整段仍已分配，不适用于部分释放后的分配 |
| page2pa/pa2page | pages[0] 对应 DRAM_BASE，不对应物理地址零 |
| PADDR/KADDR/page2kva | 集中转换；数据访问不要直接解引用物理地址数值 |
| kmalloc/kfree | 零大小返回 NULL，释放 NULL 无操作，最少 16 字节对齐 |
| run_selftests | 独立测试入口，集成后可单独控制 |

## 公共结构与状态

Page 包含指导书的 ref、flags、property、page_link，另有 alloc_span/requested 记录实际跨度和请求长度，当前 sizeof(Page)=56。改变结构后须重新计算描述符占用、跑布局测试，不要硬编码空闲页数。

flags 按掩码使用：1=reserved，2=空闲块头，4=allocated，8=SLUB 所有权。与使用位序编号的框架不能直接混用。SLUB 所有权只标记分配头；归还物理页时清除。

Fit 空闲头的 property 为页数；Buddy 空闲头的 property 为阶，order=0 合法，必须同时判断 PageProperty。非头页不挂空闲链表。当前单 hart、无锁，不提供 SMP 保证。

## A 接入分页时

1. 当前 KERNEL_VA_PA_OFFSET=0、satp=0。先保证启动代码、栈、DTB 和内核数据在切换时可访问，再进入虚拟地址 C 代码。
2. 配置直接映射偏移，示例为 0xffffffff40000000；va_pa_offset 在 page_init 设置。PMM 转换接口不可早于 pmm_init 使用。
3. DTB 可能位于受管 128 MiB 之外，256 MiB 配置下尤其如此。映射必须覆盖 DTB，或切换前解析/复制，不能只映射已管理页。
4. end 是链接地址；描述符在 end 后对齐放置，物理位置由偏移转换。检查与内核、页表和保留区不重叠。
5. 页表页用 alloc_page 分配，用 page2kva 写内容，用 page2pa 填 PTE；撤映射和 TLB 刷新由 A 负责。
6. 继承 riscv.h 存在历史 VM_SV39=9 宏；现代 RV64 satp 的 Sv39 MODE 是 8，不应不加核对地套用旧宏。B 未使用或修改它。

## C 集成验收

- 当前顺序：清 BSS → 首条启动输出 → DTB → PMM → 自建测试 → 空循环；未新增时钟中断流程。
- 构建参数 PMM 选后端，默认 Best-Fit。Buddy 的释放参数契约须在调用者中保留，尤其是 SLUB 大对象路径。
- 合并公共头时保留所有权检查，或提供等价机制，不能因冲突直接删断言。
- 当前没有课程 basic_check/best_fit_check；取得后原样接入，先核对状态约定，不修改原测试迁就实现。
- 跑 grade-all、grade-boot；分页后调整继承的低地址启动断言，新增高地址、权限和 TLB 验证。Bare 结果不能直接外推。
- QEMU 源码、调试符号、宿主 GDB 附加及地址翻译调用路径仍属 C，本次没有这些调试记录。
- 用户没有原仓库写权限，复测后的 lab2-b-pmm 推送到 dowser47 的 fork，再向原仓库 main 提交 Pull Request，由维护者决定合并；提交信息使用英文具体说明改动，远端状态以 GitHub PR 为准。
