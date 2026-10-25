# Lab2 B：运行说明

负责人：2414067—窦思微。源码从仓库 lab1/code 复制，保留启动、串口/SBI 和构建基础，再补齐 B 的物理内存管理；lab1 未修改。

**当前为 Bare 模式下的 B 独立验证，不是 A/C 完成集成后的完整 Lab2。**不写 satp，不建立虚拟地址映射。测试均为自建，没有课程提供的 basic_check/best_fit_check 或官方评分脚本。

## 环境

WSL2 Ubuntu 24.04.4；RISC-V GCC 13.2.0、binutils 2.42、QEMU 8.2.2、gdb-multiarch 15.1、Make 4.3、Python 3.12.3。直接使用当前软件源版本，无降级。

其他 Ubuntu 环境可安装：

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends make python3 \
    gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
    qemu-system-misc gdb-multiarch
```

## 编译与验证

在本目录执行：

```bash
make                         # 默认 Best-Fit
make grade                   # 默认 Best-Fit / 128 MiB，自建功能测试
make grade-all               # 三种后端×三种容量 + 26 个预期失败，共 35 场景
make grade-boot              # 从 lab1 继承并调整路径的 GDB 启动检查
make PMM=buddy grade
make PMM=first_fit MEMORY=64M grade
make qemu                    # 人工观察；结束后内核停在循环
```

`PMM` 可取 first_fit、best_fit、buddy。构建产物位于 `obj/<PMM>/<TEST_CASE>/` 和 `bin/<PMM>/<TEST_CASE>/`，防止宏配置污染；`make clean` 只清理当前配置。TEST_CASE=0 为正常测试，1～10 为自建负面测试（4、9 只用于 Buddy）。MEMORY 的自动验证支持 64M、128M、256M。

`grade-all` 检查管理器名称、DTB 容量、管理上界、成功标志和指定断言；失败返回非零。内核没有自动退出协议，runner 结束其启动的 QEMU，因此日志尾部 signal 15 是正常清理。负面场景须先出现对应 EXPECT 标记，再命中指定断言，不能因任意 panic 而通过。

证据：`evidence/baseline/` 是 B 实现前的继承验证，`evidence/boot/` 是当前代码的 GDB 启动检查，`evidence/selftest/` 是 B 功能检查及构建记录。完整汇总为其中 summary-all.json，complete=true 才表示 35 场景结束。

## 手动 GDB

两个终端进入本目录，分别运行 `make debug`、`make gdb`：

```gdb
b kern_init
c
info registers a0 a1 satp
b run_selftests
c
p *pmm_manager
p npage
p nbase
p/x free_begin
p/x managed_end
```

当前 satp=0。a0/a1 在入口分别为 hart ID、DTB 物理地址，之后会被函数调用改变。自动启动验证使用临时本机端口；手动调试使用 1234。退出 QEMU 按 Ctrl+A，松开后按 X。

## 使用边界

- QEMU virt、单 hart、连续 DRAM 从 0x80000000 开始，最多管理到 0x88000000；256 MiB 启动仅管理低 128 MiB，并打印截断。
- 解析 FDT v17，支持 1/2-cell RAM 字段；不连续 bank 显式拒绝。不是通用固件或任意 reserved-memory 管理器。
- 固件保留区沿用课程 [0x80000000,0x80200000)，不能直接部署于任意真实硬件。
- First/Best-Fit 支持分批释放；Buddy 必须使用原始分配头和请求长度。allocation_pages 只查询未被部分释放的完整分配。
- SLUB 是教学简化版本，没有 per-CPU、并发保护、NUMA 或 Linux 完整回收策略。
- 公共接口和分页交接要求见 ../report/handoff.md。
