# Lab2：物理内存管理与启动分页

- [编译、运行和验证](code/README.md)
- [B 实验报告](report/report.md)
- [实施前 spec 与 prompt](report/prompt.md)
- [A/C 接口交接](report/handoff.md)
- [全部自建测试结果](code/evidence/selftest/summary-all.json)
- [lab1 原始文件 SHA256](report/lab1-sha256.json)

负责人（B）：2414067—窦思微。开发分支：lab2-b-pmm。

## 分页机制（指导书实现）

- [实现说明](report/report-a.md)
- [启动日志](code/evidence/paging/boot.log)
- [256 MiB DTB 日志](code/evidence/paging/boot-256M.log)
- [GDB satp 记录](code/evidence/paging/gdb-boot.log)
- [验证摘要](code/evidence/paging/summary.txt)

验收范围：B 在 Bare 模式下完成物理内存管理；A 已按指导书接入启动分页（Sv39、启动页表、satp 打印），没有加入通用页表 API；C 的 QEMU 源码双重调试仍待完成。课程原测试尚未取得，不能据此声称完整实验已通过课程评分。代码与报告保留在 lab2，未修改 lab1；B 阶段的 lab2-b-pmm 分支通过 dowser47 的 fork 向原仓库提交 Pull Request（#2），具体状态以 GitHub PR 为准。
