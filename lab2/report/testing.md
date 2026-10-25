# C 测试记录

日期：2026-10-25。所有检查为本项目自建验证，课程原评分脚本缺失。完整环境见 [environment.txt](../code/evidence/integration/boot-best_fit-128M/environment.txt)。

## 1. 命令与结果

在 `lab2/code` 中执行：

```bash
make -j2
make grade-all
python3 tools/integration.py --boot-only
make grade-source
python3 tools/check_submission.py
```

首次复现建议以 `make grade-integration` 代替中间两条：它先运行 35 个 PMM 场景，再运行全部 9 个 GDB 场景。`--boot-only` 仅复用已存在且 complete=true 的 PMM 汇总，适合本次已经跑完整 PMM 后的启动复测，不能用作首次全量验证。

| 结果 | 证据 |
|---|---|
| 编译通过，-Wall/-Werror | [build.log](../code/evidence/integration/build.log)，各后端/测试构建日志在 selftest/ |
| 35/35 PMM 场景 | [matrix.log](../code/evidence/integration/matrix.log)、[summary-all.json](../code/evidence/selftest/summary-all.json) |
| 9/9 GDB 启动场景 | [boot-matrix.log](../code/evidence/integration/boot-matrix.log)、[summary.json](../code/evidence/integration/summary.json) |
| 源码双 GDB 及调试后内核自测 | [source-debug.log](../code/evidence/integration/source-debug.log)、[qemu-mmu/](../code/evidence/qemu-mmu/) |
| lab1 内容不变、所有仓库改动仅在 lab2 | [submission-check.json](submission-check.json) |

正向容量/后端组合为 First-Fit、Best-Fit、Buddy 各运行 64、128、256 MiB。负向为每后端 8 个公共非法调用，Buddy 另有错误长度和内部页，共 26 次独立启动。预期拒绝只有命中相应 EXPECT 标记和指定断言才通过；超时、任意 panic 或“错误操作返回”都不能通过。

## 2. 分页与初始化检查

boot.gdb 在复位验证 satp=0 及镜像装载，随后停到 OpenSBI 和物理 0x80200000；进入高地址 kern_init 后验证 MODE=8、PPN、SP、a0/a1。根表只有项 2/511 有效，映射覆盖低恒等区、高直接映射两端、内核入口及 DTB，未映射区的 V=0。然后验证首次打印前 BSS 为零、SBI ecall 从 S 陷入 M 并返回，最后核对 PMM 描述符/空闲区边界。

脚本 detach 后还要等待 `LAB2 B SELFTESTS PASSED`，不会仅凭首行启动文字就宣告成功。9 组都有各自的 environment、gdb、qemu 日志。256 MiB 时 DTB 位于 128 MiB 受管区外，验证它仍能通过启动直接映射访问。

## 3. 失败记录与修复后的复测

保留原失败记录，便于说明为什么修改脚本：

- `boot-before.log`：旧 lab1 高地址符号断点超时；修改启动检查后通过。
- `matrix-before-case10-fix.log`：高地址模式下非法低指针先触发 VA 检查；修正断言预期后全部 35 场景通过。
- `boot-matrix-before-crlf-fix.log`：9 组启动通过后，lab1 的 CRLF/LF 哈希差异导致边界检查误报；规范化校验后重新运行 9 组和边界检查通过。

CRLF 处理只用于对照已有 LF 哈希，未改 lab1 文件，也未更新基线哈希来迁就文件内容。另由 Git 差异范围检查保证没有 lab1 或根目录改动。

## 4. 测试边界

当前只运行单 hart、单连续 DRAM bank 的 QEMU virt。没有课程原测试、通用 map/unmap/query API、三级小页遍历现场、真实用户权限缺页、A/D 更新、多核 shootdown、并发分配或物理硬件测试。本次结果适用于记录的构建与场景，不据此推断生产级内存安全或所有硬件兼容性。

QEMU 在自测后进入空循环，由 runner 终止，日志末尾 signal 15 是清理行为。源码调试先 detach 原生 GDB 再让客户机完成测试，最后清理自己创建的进程，不需要人为停止其他 QEMU 会话。
