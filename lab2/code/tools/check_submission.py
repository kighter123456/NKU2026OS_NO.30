#!/usr/bin/env python3
"""lab2: 2414067. Verify protected lab1 and the repository modification boundary."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

def main():
    repo = Path(__file__).resolve().parents[3]
    baseline = json.loads((repo / "lab2/report/lab1-sha256.json").read_text())
    normalized_files = []
    for relative, expected in baseline.items():
        data = (repo / relative).read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            # Git on Windows may check out LF blobs as CRLF. Keep lab1 untouched.
            if hashlib.sha256(data.replace(b'\r\n',b'\n')).hexdigest() != expected:
                raise RuntimeError(f"protected file changed: {relative}")
            normalized_files.append(relative)
    tracked = subprocess.check_output(["git", "ls-files", "lab1"], cwd=repo, text=True).splitlines()
    if set(tracked) != set(baseline):
        raise RuntimeError("lab1 tracked-file set changed")
    changed = set()
    for command in (["git", "-c", "core.safecrlf=false", "diff", "--name-only", "HEAD"],
                    ["git", "ls-files", "--others", "--exclude-standard"]):
        changed.update(subprocess.check_output(command, cwd=repo, text=True).splitlines())
    if any(not name.startswith("lab2/") for name in changed):
        raise RuntimeError("repository changes outside lab2: " + repr(sorted(changed)))
    summary = json.loads((repo / "lab2/code/evidence/selftest/summary-all.json").read_text())
    if not summary.get("complete") or len(summary["results"]) != 35:
        raise RuntimeError("full self-written matrix is incomplete")
    result = {"lab1_files_unchanged": len(baseline), "repository_changes_only_lab2": True,
              "crlf_normalized_for_hash": normalized_files,
              "self_written_scenarios": len(summary["results"]), "course_tests_available": False}
    integration = repo / 'lab2/code/evidence/integration/summary.json'
    if integration.exists():
        boot = json.loads(integration.read_text())
        if not boot.get('complete') or len(boot['boot_gdb_results']) != 9:
            raise RuntimeError('C boot matrix incomplete')
        result['guest_gdb_boot_scenarios'] = 9
    source_log = repo / 'lab2/code/evidence/qemu-mmu/host-gdb.log'
    if source_log.exists():
        source = source_log.read_text()
        for marker in ('QEMU SOURCE MMU CHECKS PASSED','PASS: subsequent fetch hits software TLB',
                       'PASS: Sv39 store fills software TLB'):
            if marker not in source:
                raise RuntimeError('C QEMU source debug incomplete: '+marker)
        result['qemu_source_debug'] = 'Bare/Sv39, TLB miss/fill/hit, BSS store'
    (repo / "lab2/report/submission-check.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS: " + json.dumps(result, ensure_ascii=False))

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
