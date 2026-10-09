# NKU2026OS_NO.30

南开大学 2026 操作系统实验小组仓库。实验源码、报告和验证记录位于 `lab1/` 子目录。

已在 Windows 的 Ubuntu 22.04（WSL2）中完成环境配置、内核编译、QEMU 运行及 GDB 启动跟踪。实验答案见 [lab1 实验报告](lab1/lab1-report.md)，原始记录见 [evidence](lab1/evidence/)。

## 在 Windows 中运行

在 PowerShell 中执行：

```powershell
# 编译并验证两个练习涉及的启动行为
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1\lab1" --exec make grade

# 运行内核，看到启动信息后按 Ctrl+A，松开后再按 X 退出
wsl -d Ubuntu --cd "E:\桌面\OSLab\lab1\lab1" --exec make qemu
```

验证成功时显示：

```text
PASS: reset ROM -> OpenSBI -> kern_entry -> kern_init -> SBI console
```

## 在 Ubuntu 中运行

```bash
cd /mnt/e/桌面/OSLab/lab1/lab1
make
make grade
make qemu
```

在其他 Ubuntu 22.04 环境中，可先安装这些软件包；当前电脑已经安装完成：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends make python3 \
    gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
    qemu-system-misc gdb-multiarch
```

`make gdb` 优先使用 `riscv64-unknown-elf-gdb`，未安装时自动使用 `gdb-multiarch`。当前 QEMU 6.2 满足指导书要求的 4.1 以上版本，无需编译旧版 QEMU。

## 手动调试

在两个 Ubuntu 终端中，均先进入仓库的 `lab1/` 子目录。第一个执行 `make debug`，第二个执行 `make gdb`。进入 GDB 后：

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

自动验证采用临时本机端口，结束时会关闭它启动的 QEMU；手动调试使用端口 1234。

## 交付文件

| 文件 | 内容 |
| --- | --- |
| `lab1/lab1-report.md` | lab0 环境、两个练习答案、模块分析、OS 原理对照及实测结论 |
| `lab1/bin/kernel` | 带调试符号的 RISC-V ELF 内核 |
| `lab1/bin/ucore.img` | 可供 QEMU 加载的原始内核镜像 |
| `lab1/tools/boot.gdb` | 从复位到 SBI 输出的断点、单步与断言 |
| `lab1/tools/verify.py` | 启动 QEMU/GDB、保存记录、检查输出并回收进程 |
| `lab1/evidence/` | 编译、运行、调试及修复前后的原始输出 |

`make grade` 是本次补充的启动验证，不是课程官方评分脚本；提供的基础环境缺少原 Makefile 引用的 `tools/grade.sh`。验证覆盖当前 lab1 的两个练习，不代表后续实验已经完成。

## Git 仓库

小组仓库：[NKU2026OS_NO.30](https://github.com/kighter123456/NKU2026OS_NO.30)。源码、实验报告和原始验证记录纳入版本管理；离线指导书 `lab2026-book/` 及其压缩包 `lab2026-book.zip` 仅保留在本地，不上传。`bin/`、`obj/` 及 Python 缓存也由 `.gitignore` 排除，克隆后运行 `make` 即可重新生成内核镜像。
