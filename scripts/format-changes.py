#!/usr/bin/env python3
"""Check or format changed C++ lines, including untracked files, with clang-format 22.1.8."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def git(*args):
    return subprocess.check_output(["git", *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="HEAD", help="Compare against this Git revision")
    parser.add_argument("--fix", action="store_true", help="Apply formatting in place")
    args = parser.parse_args()
    os.chdir(Path(__file__).resolve().parent.parent)
    binary = os.environ.get("CLANG_FORMAT", "clang-format")
    version = subprocess.check_output([binary, "--version"], text=True)
    if not re.search(r"\bversion 22\.1\.8\b", version):
        sys.exit("Use clang-format 22.1.8 (set CLANG_FORMAT to its executable path).")

    changed = git("diff", "--name-only", "-z", "--diff-filter=ACMR", args.base, "--", "src", "tests")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z", "--", "src", "tests")
    new_files = set(untracked.decode().split("\0"))
    failed = False
    for name in sorted(set(changed.decode().split("\0")) | new_files):
        if not name or Path(name).suffix not in {".cpp", ".h"}:
            continue
        command = [binary, "--style=file"]
        if name not in new_files:
            diff = git("diff", "--no-ext-diff", "--no-color", "--unified=0", args.base, "--", name)
            hunks = re.findall(rb"^@@ .*? \+(\d+)(?:,(\d+))? @@", diff, re.MULTILINE)
            for start, count in hunks:
                length = int(count) if count else 1
                if length:
                    command.append(f"--lines={int(start)}:{int(start) + length - 1}")
            if len(command) == 2:
                continue
        command += ["-i"] if args.fix else ["--dry-run", "--Werror"]
        result = subprocess.run([*command, name], check=False)
        failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
