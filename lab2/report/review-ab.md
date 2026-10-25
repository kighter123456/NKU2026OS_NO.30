# C 对 A/B 交付的核查

核查基线：2026-10-25 拉取 main 至 `52ae7d0`。B 的提交 `21a0ded` 已通过 PR #2 合并，A 的提交 `561f36c` 已通过 PR #3 合并。C 没有重复执行合并或改写其历史。

验收同时对照用户提供的《Lab分工.md》与本地指导书，区分“代码可运行”“指导书范围”和“小组分工承诺”。附件用于识别任务范围，其中的协作建议不视作用户要求重新 fork/提交 PR 的命令。

## A：启动分页可用，严格分工未完全交付

| 分工条目 | 仓库证据及核查结论 |
|---|---|
| 内核启动分页、satp、TLB 刷新 | entry.S、kernel.ld、memlayout.h 已实现；C 实测 MODE=8、高 PC/SP、根 PA、sfence.vma 后运行正常 |
| 页对齐、权限 | 根按 4096 对齐，1 GiB 叶子 PPN 对齐；V/R/W/X/A/D、U=0。C 只读检查两个有效根项及边界通过 |
| 低地址切换窗口 | 保留根项 2 的恒等映射，切换 satp 后取指可继续；A 的历史 QEMU 10.2 故障记录解释其必要性 |
| 通用多级遍历/查询模块 | 未交付；C 的 GDB walker 是测试工具，不是内核 query API |
| 建立/取消映射 API | 未交付 map/unmap API，也没有失败回滚或动态刷新测试 |
| 独立 map/unmap/query 测试 | 未交付；已有证据仅验证启动映射，二者不能互相替代 |
| 报告、AI 记录 | report-a.md 已有。其收紧范围的决定由 A 的历史记录说明，C 不擅自把缺项判成已完成 |

**结论**：按 A 自述和指导书的启动分页小节可验收；按所附分工文档的全部条目不完备。C 已完成实际启动映射验证与缺项记录，未新增 A 的通用页表功能。小组若仍以完整分工列表验收，需要 A 后续补交或共同确认范围。

## B：功能和自建测试完整，原课程测试未验收

| 分工条目 | 仓库证据及核查结论 |
|---|---|
| 物理内存探测/布局 | dtb.c、pmm.c；64/128/256 MiB 均解析正确，管理上限打印清楚 |
| Page 与空闲块管理 | 单一 Page 定义，内核/描述符保留，块计数与所有权检查已实现 |
| 分配/释放及地址转换 | 三种分配器、page2pa/pa2page、PADDR/KADDR；C 核对集成后高地址契约 |
| 分配失败、边界及重复释放 | OOM、独立随机位图、负面断言场景；完整 35 场景在 Sv39 下重新通过 |
| 扩展 Buddy/SLUB | 已实现并有实现/测试两组 prompt；简化 SLUB 不等同 Linux 完整实现 |
| 报告、AI 记录 | 原报告保留为 report-b.md，prompt.md 包含各任务及独立测试要求 |
| 课程 basic_check/best_fit_check | 仓库未提供；自建测试明确标注，不能宣称原测试通过 |

**结论**：B 的模块、说明和自建测试已交付，集成后功能回归通过；课程原始框架兼容性仍未独立验收。没有为了适配 A 删除 B 的内核断言。

## 集成发现、修复与证据路径

| Evidence（证据） | Finding（发现） | Path（修复/验证） |
|---|---|---|
| integration/boot-before.log | 继承脚本在高地址 kern_entry 断点等待超时 | boot.gdb 分别使用物理入口与高地址 C 入口；9 组新 GDB 检查通过 |
| 历史报告与 pmm.c/runner | 已开 Sv39，却仍打印及匹配 Bare | pmm.c、runner、当前 README 统一为 Sv39；A/B 原报告作为历史保留 |
| libs/riscv.h | 旧 VM 编码把 Sv39 写成 9 | 更新为 satp.MODE=8；Sv32=1、Sv48=9，启动汇编已有 MODE=8 |
| integration/matrix-before-case10-fix.log | 低非法 VA 提前触发 VA guard，旧 runner 只接受 PA guard | 指定期待 v>=va_pa_offset；仍要求对应 EXPECT 标记和实际断言 |
| integration/boot-matrix-before-crlf-fix.log | Windows CRLF 导致未修改的 lab1 内容校验误报 | 只在计算哈希时兼容 LF 基线，另核查 Git 修改范围；不写入 lab1 |

表内证据目录均相对 `lab2/code/evidence/`。最新结论由 [整合报告](report.md)、[测试记录](testing.md)及原始日志共同支撑。
