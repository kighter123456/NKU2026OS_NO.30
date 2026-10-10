#!/usr/bin/env python3
"""Run the lab1 boot exercises and retain the actual QEMU/GDB output."""

import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent.parent
MESSAGE = "(THU.CST) os is loading ..."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--gdb", default="gdb-multiarch")
    args = parser.parse_args()
    os.chdir(ROOT)
    for tool in (args.qemu, args.gdb):
        if not shutil.which(tool):
            parser.error("Missing tool: " + tool)
    if not Path("bin/ucore.img").is_file():
        parser.error("Build the kernel with make first")

    evidence = ROOT / "evidence"
    evidence.mkdir(exist_ok=True)
    with (evidence / "environment.txt").open("w") as output:
        for command in (["uname", "-a"], ["make", "--version"],
                        ["riscv64-unknown-elf-gcc", "--version"],
                        [args.qemu, "--version"], [args.gdb, "--version"]):
            subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                           check=True, timeout=10)

    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]

    command = [args.qemu, "-machine", "virt", "-nographic", "-bios", "default",
               "-kernel", "bin/ucore.img",
               "-gdb", "tcp:127.0.0.1:" + str(port), "-S"]
    with (evidence / "qemu.log").open("w") as output:
        qemu = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while True:
                if qemu.poll() is not None:
                    raise RuntimeError("QEMU exited before GDB connected")
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("QEMU did not open its GDB port")
                    time.sleep(0.05)

            with (evidence / "gdb.log").open("w") as trace:
                subprocess.run(
                    [args.gdb, "-q", "-nx", "-batch", "bin/kernel",
                     "-ex", "target remote 127.0.0.1:" + str(port),
                     "-x", "tools/boot.gdb"],
                    stdout=trace, stderr=subprocess.STDOUT, check=True, timeout=45)

            deadline = time.monotonic() + 5
            while MESSAGE not in (evidence / "qemu.log").read_text(errors="replace"):
                if qemu.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("The kernel did not print its startup message")
                time.sleep(0.05)
        finally:
            qemu.terminate()
            try:
                qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait(timeout=5)

    trace = (evidence / "gdb.log").read_text(errors="replace")
    if "LAB1 BOOT CHECKS PASSED" not in trace:
        raise RuntimeError("GDB did not complete the boot checks")
    print("PASS: reset ROM -> OpenSBI -> kern_entry -> kern_init -> SBI console")
    print("Logs: evidence/environment.txt, evidence/gdb.log, evidence/qemu.log")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        print("Inspect evidence/gdb.log and evidence/qemu.log", file=sys.stderr)
        sys.exit(1)
