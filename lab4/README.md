# Lab4：进程管理

**三个功能模块全部实现，`make grade` 30/30。**

## 目录

- `code/`：Lab4 源码（含 Lab2 物理内存/页表与 Lab3 中断框架）。
- `report/report.md`：按指导书要求撰写的实验报告。
- `report/evidence/`：正常启动日志、评分输出、QEMU 10.2 非叶 PTE 故障复现日志。

## 实现内容与分工

- 功能模块一（2414067）：`page_insert` / `page_remove` / `page_remove_pte`，`kern/mm/pmm.c`。
- 功能模块二（2412267）：`alloc_proc` / `proc_init` / `kernel_thread` / `do_fork` / `copy_thread` / `copy_mm`。
- 功能模块三（2411520）：`cpu_idle` / `proc_run` / `schedule` / `switch_to`。

## 验证

```bash
cd code
make clean && make
make qemu     # check_pgdir / check_boot_pgdir / check_vmm + initproc 输出
make grade    # Total Score: 30/30
```

`make qemu` 最后出现 `panic: process exit!!.` 是 Lab4 的预期行为：`init_main` 返回后
`kernel_thread_entry` 调用 `do_exit`，而 Lab4 的 `do_exit` 仍是 panic。不是实验失败。

## 环境适配（QEMU 10.2 / GCC 15）

- `tools/grade.sh` 使用 `-kernel bin/ucore.img`，替代 QEMU 10.2 下无法启动的 `-device loader`。
- `get_pte()` 创建中间级页表项时只设置 `PTE_V`：RISC-V 规范要求非叶 PTE 的 D/A/U 为 0，
  QEMU 10.2 会严格检查。
- `tools/test_lab4.c` 的测试线程返回 `int`，与 `kernel_thread` 的接口一致（GCC 14+ 将
  不兼容函数指针升级为错误）。

详细现象、定位过程和修复见 `report/report.md` 第 7、11 节与 `report/evidence/`。
