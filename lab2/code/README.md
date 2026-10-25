# Lab2 编译、运行与复现

当前内核在物理 0x80200000 启动，切换 Sv39 后进入高地址 C 入口，再解析 DTB、初始化 PMM 并运行分配器/简化 SLUB 自测。默认 Best-Fit，可切换 First-Fit/Buddy。所有验证为自建，未取得课程原始 basic_check/best_fit_check。

本次验证环境：WSL2 Ubuntu 22.04.1，RISC-V GCC 10.2.0、binutils 2.35.1、QEMU 6.2.0/OpenSBI 0.9、GDB 12.1、Make 4.3、Python 3.10.6。A/B 的历史环境记录另见各自报告。

## 安装及运行

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends make python3 gcc-riscv64-unknown-elf \
  binutils-riscv64-unknown-elf qemu-system-misc gdb-multiarch
make -j2
make qemu
```

QEMU 最后停在内核空循环；退出按 Ctrl+A，松开后按 X。在此 Windows 主机中可从 PowerShell 执行：

```powershell
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab2\code" --exec make grade-integration
```

## 自动检查

```bash
make grade                       # 当前后端/容量的正常功能自测
make grade-all                   # 35 个 PMM/SLUB 正负场景
make grade-boot                  # 当前后端/容量的分页启动 GDB 检查
make grade-integration           # 35 个 PMM 场景 + 9 个 GDB 启动场景 + 提交边界
make PMM=buddy MEMORY=256M grade
make PMM=first_fit MEMORY=64M grade-boot
python3 tools/check_submission.py
```

`PMM` 为 first_fit、best_fit、buddy；`TEST_CASE=0` 为正常测试，1～10 为负面场景，4/9 仅用于 Buddy。构建目录按后端/场景隔离：`obj/<PMM>/<TEST_CASE>/`、`bin/<PMM>/<TEST_CASE>/`。容量验证支持 64M/128M/256M。`make clean` 只清当前配置。

`QEMU` 可指定系统/自编译可执行文件，GDB 用交叉 gdb（若安装）或 gdb-multiarch；Python 入口也支持显式传参。自动启动使用临时本机端口。负面测试只有命中对应 EXPECT 标记及指定断言才通过；正常场景须完整输出自测成功，runner 清理进程时的 signal 15 为正常结束。

## QEMU 源码双 GDB

```bash
sudo apt-get install --no-install-recommends build-essential pkg-config libglib2.0-dev \
  libpixman-1-dev zlib1g-dev curl xz-utils gdb
bash tools/build-debug-qemu.sh
make grade-source
```

下载并校验上游 QEMU 6.2.0，在缓存目录编译 `--enable-debug`，不执行 make install。默认调试二进制为 `$HOME/.cache/oslab/qemu-6.2.0/build-debug/qemu-system-riscv64`；其他路径通过 `QEMU_DEBUG` 指定。此源码调试脚本依赖 6.2.0 的 C 行号，不能直接用于其他版本。

宿主 x86-64 GDB 单步 QEMU C 代码，gdb-multiarch 同时控制 RISC-V 客户机；跟踪 Bare/Sv39、页表项、TLB miss/fill/hit 和 memset 实际字节写入。完整操作与解析见 [gdb-mmu.md](../report/gdb-mmu.md)。源码在仓库外缓存，不随 Git 上传。

## 手动客户机 GDB

两个终端分别执行 `make debug` 和 `make gdb`，然后：

```gdb
thbreak *0x80200000
continue
info registers pc a0 a1 satp
thbreak kern_init
continue
info registers pc sp satp
thbreak run_selftests
continue
p *pmm_manager
p/x free_begin
p/x managed_end
```

物理入口时 satp=0，kern_init 时 MODE=8；不要在分页前使用 kern_entry 的高链接地址作为实际入口断点。手动端口为 1234。

## 证据及边界

当前证据：`evidence/selftest/summary-all.json`（35 场景）、`evidence/integration/summary.json`（9 组 GDB）、`evidence/qemu-mmu/`（源码双 GDB）。历史证据在 `baseline/`、`paging/`，旧版本/旧阶段日志不等同本轮验证。`evidence/boot/` 保存单配置启动检查。

QEMU virt、单 hart、单连续 RAM bank，管理范围最多到 0x88000000，256 MiB 启动只管理低 128 MiB。DTB 解析不是通用硬件保留区管理器。First/Best-Fit 支持分批释放，Buddy 需原始头和请求长度，简化 SLUB 无并发/NUMA 支持。A 的通用 map/unmap/query 尚未交付，当前只验证启动映射。详见 [整合报告](../report/report.md)、[A/B review](../report/review-ab.md)和 [测试记录](../report/testing.md)。
