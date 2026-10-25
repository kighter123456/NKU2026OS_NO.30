# Lab2：物理内存管理与启动分页

已集成 A 的 Sv39 启动分页和 B 的三种页分配器/简化 SLUB；C 完成集成回归、实际 QEMU 源码双 GDB 调试和报告汇总。

- [整合实验报告](report/report.md)
- [编译与复现说明](code/README.md)
- [C 对 A/B 完整性的核查](report/review-ab.md)
- [C 的 QEMU 页表/TLB 调试记录与截图](report/gdb-mmu.md)
- [测试记录](report/testing.md)
- [AI 协作记录汇总](report/ai-collaboration.md)
- [A 原始说明](report/report-a.md)、[B 原始报告](report/report-b.md)、[B 实施前提示词](report/prompt.md)
- [35 个自建功能测试](code/evidence/selftest/summary-all.json)、[9 个 GDB 启动测试](code/evidence/integration/summary.json)
- [提交边界检查](report/submission-check.json)

本轮基于 main 的 `52ae7d0`，B PR #2、A PR #3 已合并。A 的启动分页可运行，但分工文档中的通用 map/unmap/query 和独立测试未交付；B 已完成自建验证，课程原测试仍缺失。当前结果不能称为所有分工条目或课程评分全部通过。修改限定 lab2，lab1 内容保持原样。
