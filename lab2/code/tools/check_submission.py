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
    for relative, expected in baseline.items():
        actual = hashlib.sha256((repo / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"protected file changed: {relative}")
    tracked = subprocess.check_output(["git", "ls-files", "lab1"], cwd=repo, text=True).splitlines()
    if set(tracked) != set(baseline):
        raise RuntimeError("lab1 tracked-file set changed")
    changed = set()
    for command in (["git", "diff", "--name-only", "HEAD"],
                    ["git", "ls-files", "--others", "--exclude-standard"]):
        changed.update(subprocess.check_output(command, cwd=repo, text=True).splitlines())
    if any(not name.startswith("lab2/") for name in changed):
        raise RuntimeError("repository changes outside lab2: " + repr(sorted(changed)))
    summary = json.loads((repo / "lab2/code/evidence/selftest/summary-all.json").read_text())
    if not summary.get("complete") or len(summary["results"]) != 35:
        raise RuntimeError("full self-written matrix is incomplete")
    result = {"lab1_files_unchanged": len(baseline), "repository_changes_only_lab2": True,
              "self_written_scenarios": len(summary["results"]), "course_tests_available": False}
    (repo / "lab2/report/submission-check.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS: " + json.dumps(result, ensure_ascii=False))

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
