#!/usr/bin/env python3
"""Keep protocol code independent of Bitmain work and hardware components."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
BITMAIN = re.compile(
    r"\b(?:bm_job|BM_JOB_\w+|[Bb][Mm]\d+\w*|construct_bm_job_from_miner_job|"
    r"free_bm_job|midstate_sha256_bin|hashCounterToGhs)\b"
)
HARDWARE_INCLUDE = re.compile(r'#\s*include\s*[<"](?:asic\.h|bm[^/]*\.h)[>"]')


def main():
    paths = []
    for component in ("stratum", "stratum_v2", "mining_job"):
        paths.extend(
            path for path in (ROOT / "components" / component).rglob("*")
            if path.suffix in (".c", ".h", ".cmake") or path.name == "CMakeLists.txt"
        )
    paths.extend((ROOT / "main" / "tasks").glob("stratum*.c"))
    paths.extend((ROOT / "main" / "tasks").glob("stratum*.h"))

    errors = []
    for path in sorted(paths):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            forbidden = BITMAIN.search(line) or HARDWARE_INCLUDE.search(line)
            if path.name == "CMakeLists.txt" or path.suffix == ".cmake":
                forbidden = forbidden or re.search(r"\basic\b", line)
            if forbidden:
                errors.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")

    public_asic = ROOT / "components" / "asic" / "include" / "asic.h"
    for number, line in enumerate(public_asic.read_text().splitlines(), 1):
        if BITMAIN.search(line) or re.search(r"\bASIC_send_work\b", line):
            errors.append(f"{public_asic.relative_to(ROOT)}:{number}: {line.strip()}")

    if errors:
        print("Bitmain implementation leaked into a common or protocol boundary:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Mining boundary check passed: Stratum and the common ASIC API contain no Bitmain work.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
