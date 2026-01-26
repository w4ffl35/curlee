#!/usr/bin/env python3

from __future__ import annotations

import argparse
import random
from pathlib import Path


def gen_expr(rng: random.Random, depth: int) -> str:
    if depth <= 0 or rng.random() < 0.4:
        return str(rng.randint(0, 1000))
    left = gen_expr(rng, depth - 1)
    right = gen_expr(rng, depth - 1)
    expr = f"{left} + {right}"
    if rng.random() < 0.5:
        return f"({expr})"
    return expr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=500)
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--out", type=Path, default=Path("tests/correct_samples"))
    parser.add_argument("--training", type=Path, default=Path("training_data.txt"))
    args = parser.parse_args()

    rng = random.Random(args.seed)
    out_dir: Path = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    samples = []
    for idx in range(1, args.count + 1):
        expr = gen_expr(rng, depth=3)
        program = "\n".join(
            [
                "fn main() -> Int {",
                f"  return {expr};",
                "}",
                "",
            ]
        )
        name = f"sample_{idx:04d}.curlee"
        (out_dir / name).write_text(program, encoding="utf-8")
        samples.append(program.strip())

    header = f"# Curlee correct_samples dataset\n# seed={args.seed}\n# count={args.count}\n"
    args.training.write_text(header + "\n---\n".join(samples) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
