# Lab1：最小可执行内核与启动流程

本分支 `lab1` 按课程要求交付两个目录：`code/` 保存实验源码和验证记录，`report/` 保存报告、提示词及测试截图。

- [实验报告](../report/report.md)
- [提示词汇总](../report/prompt.md)
- [测试截图](../report/images/)
- [原始测试记录](evidence/)

## 编译与运行

在 Ubuntu 中，从仓库根目录执行：

```bash
cd code
make
make grade
make qemu
```

在本机 PowerShell 中执行：

```powershell
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1\code" --exec make grade
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1\code" --exec make qemu
```

内核输出 `(THU.CST) os is loading ...` 后无限循环。退出 QEMU：按 Ctrl+A，松开后再按 X。

`make grade` 是本实验补充的启动验证：检查复位、OpenSBI 交接、内核栈、尾调用及 SBI 输出，保存日志到 `code/evidence/` 并关闭测试启动的 QEMU。它不是课程官方评分脚本；基础环境没有原 Makefile 引用的 `tools/grade.sh`。

## 环境安装

当前 Ubuntu 22.04（WSL2）已经安装相应工具。其他 Ubuntu 22.04 环境可执行：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends make python3 \
    gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
    qemu-system-misc gdb-multiarch
```

`make gdb` 优先使用 RISC-V 工具链 GDB，缺少时使用 `gdb-multiarch`。

## 手动调试

两个 Ubuntu 终端都进入 `code/`。第一个执行 `make debug`，第二个执行 `make gdb`。在 GDB 中：

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

手动调试使用端口 1234，自动验证使用临时本机端口。

## Git 提交

小组仓库：[NKU2026OS_NO.30 的 lab1 分支](https://github.com/kighter123456/NKU2026OS_NO.30/tree/lab1)。`code/bin/`、`code/obj/`、离线指导书及压缩包不纳入版本管理；克隆后运行 `make` 重新生成镜像。
