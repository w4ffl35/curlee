#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path


def iter_samples(dirs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for d in dirs:
        files.extend(sorted(d.glob("*.curlee")))
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--curlee", type=Path, default=Path("build/linux-debug/curlee"))
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--dirs", nargs="*", type=Path, default=[Path("tests/run")])
    args = parser.parse_args()

    curlee = args.curlee
    if not curlee.exists():
        raise SystemExit(f"curlee binary not found: {curlee}")

    samples = iter_samples(args.dirs)
    if not samples:
        raise SystemExit("no samples found")

    total = 0.0
    for _ in range(args.runs):
        start = time.perf_counter()
        for sample in samples:
            subprocess.run([str(curlee), "check", str(sample)], check=True)
        total += time.perf_counter() - start

    avg = total / args.runs
    print(f"samples={len(samples)} runs={args.runs} avg_seconds={avg:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
