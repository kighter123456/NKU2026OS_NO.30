#!/usr/bin/env python3
"""lab2: 2414067. Self-written QEMU checks, NOT the course grade script."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "evidence" / "selftest"
QEMU = "qemu-system-riscv64"
MANAGERS = {"first_fit": "default_pmm_manager", "best_fit": "best_fit_pmm_manager", "buddy": "buddy_pmm_manager"}
NEGATIVE = {
    1: ("zero-allocation", "n>0"),
    2: ("reserved-release", "pa>=free_begin"),
    3: ("double-page-free", None),
    4: ("buddy-wrong-size", "base->requested==n"),
    5: ("object-interior", "(offset-HEADER_SIZE)%s->unit==0"),
    6: ("double-object-free", "s->bitmap[slot/64]&bit"),
    7: ("invalid-dtb", "dtb_parse(bad,sizeof(bad),&info)==0"),
    8: ("descriptor-outside", "index<npage-nbase"),
    9: ("buddy-interior", "base->alloc_span"),
    # Under the high mapping, 0x1000 is rejected by the VA guard before PA validation.
    10: ("object-outside", "v>=va_pa_offset"),
}

def build(backend, case):
    command = ["make", "-j2", f"PMM={backend}", f"TEST_CASE={case}"]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
    (EVIDENCE / f"build-{backend}-{case}.log").write_text(
        "$ " + " ".join(command) + "\n" + result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"build failed: {backend}/{case}\n{result.stdout}{result.stderr}")

def run(backend, memory, case=0):
    name = f"{backend}-{memory}-case{case}"
    logfile = EVIDENCE / f"{name}.log"
    image = ROOT / "bin" / backend / str(case) / "ucore.img"
    command = [QEMU, "-machine", "virt", "-m", memory,
               "-nographic", "-bios", "default", "-kernel", str(image), "-no-reboot"]
    with logfile.open("w", encoding="utf-8") as output:
        output.write("$ " + " ".join(command) + "\n"); output.flush()
        proc = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 35
            while True:
                text = logfile.read_text(errors="replace")
                if re.search(r"^(?:\[PANIC\]|LAB2 B SELFTESTS PASSED:)[^\n]*\n", text, re.MULTILINE):
                    break
                if proc.poll() is not None:
                    raise RuntimeError(f"{name}: QEMU exited before test result")
                if time.monotonic() > deadline:
                    raise RuntimeError(f"{name}: timeout; see {logfile}")
                time.sleep(0.05)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait(timeout=5)
    text = logfile.read_text(errors="replace")
    if f"memory management: {MANAGERS[backend]} (Sv39 integrated)" not in text:
        raise RuntimeError(f"{name}: wrong manager")
    if case:
        label, condition = NEGATIVE[case]
        if "[EXPECT PANIC] " + label not in text or "[PANIC]" not in text:
            raise RuntimeError(f"{name}: expected rejection absent\n{text[-1800:]}")
        panic_line = next(line for line in text.splitlines() if line.startswith("[PANIC]"))
        if condition and condition not in panic_line:
            raise RuntimeError(f"{name}: unexpected panic: {panic_line}")
        if case == 3 and not any(x in panic_line for x in ("PageAllocated(base+i)", "PageAllocated(base)")):
            raise RuntimeError(f"{name}: unexpected double-free failure: {panic_line}")
        if "negative test unexpectedly returned" in text:
            raise RuntimeError(f"{name}: rejection did not occur")
    else:
        if "[PANIC]" in text or f"LAB2 B SELFTESTS PASSED: {MANAGERS[backend]}" not in text:
            raise RuntimeError(f"{name}: selftest failed\n{text[-1800:]}")
        mib = int(memory.rstrip("Mm"))
        detected = re.search(r"RAM=\[0x([0-9a-f]+),0x([0-9a-f]+)\) size=(\d+) MiB", text)
        managed = re.search(r"MANAGED: \[0x([0-9a-f]+),0x([0-9a-f]+)\) (\d+) MiB", text)
        free = re.search(r"FREE: \[0x([0-9a-f]+),0x([0-9a-f]+)\) pages=(\d+)", text)
        if not detected or not managed or not free:
            raise RuntimeError(f"{name}: missing memory layout")
        if int(detected[3]) != mib or int(detected[2], 16)-int(detected[1], 16) != mib*1024*1024:
            raise RuntimeError(f"{name}: incorrect DTB size")
        if int(managed[3]) != min(mib, 128):
            raise RuntimeError(f"{name}: incorrect managed cap")
        if (int(free[2],16)-int(free[1],16))//4096 != int(free[3]):
            raise RuntimeError(f"{name}: inconsistent free count")
    print("PASS: " + name, flush=True)
    return {"backend": backend, "memory": memory, "case": case,
            "result": "expected rejection" if case else "passed", "log": logfile.name}

def main():
    global QEMU
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--qemu", default=QEMU)
    parser.add_argument("--backend", choices=MANAGERS, default="best_fit")
    parser.add_argument("--memory", choices=("64M", "128M", "256M"), default="128M")
    args = parser.parse_args()
    QEMU = args.qemu
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    results = []
    try:
        for backend in MANAGERS if args.all else (args.backend,):
            build(backend, 0)
            for memory in ("64M", "128M", "256M") if args.all else (args.memory,):
                results.append(run(backend, memory))
        if args.all:
            for backend in MANAGERS:
                for case in (1,2,3,5,6,7,8,10) + ((4,9) if backend == "buddy" else ()):
                    build(backend, case)
                    results.append(run(backend, "128M", case))
    finally:
        (EVIDENCE / ("summary-all.json" if args.all else f"summary-{args.backend}.json")).write_text(
            json.dumps({"self_written": True, "complete": len(results)==(35 if args.all else 1),
                        "results": results}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(results)} self-written scenarios; course tests still unavailable")

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
