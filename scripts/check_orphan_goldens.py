#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Detect unreferenced .golden/.expected files in tests/."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root path")
    parser.add_argument("--format", choices=["text", "json"], default="text")
    return parser.parse_args()


def load_test_sources(repo_root: Path) -> list[Path]:
    test_dir = repo_root / "tests"
    return sorted(list(test_dir.rglob("*.cpp")) + list(test_dir.rglob("*.ipp")))


def collect_explicit_asset_refs(source_text: str) -> set[str]:
    pattern = re.compile(r"([A-Za-z0-9_./-]+\.(?:golden|expected))")
    return set(pattern.findall(source_text))


def is_implicitly_owned(repo_root: Path, rel_asset: Path) -> bool:
    rel = rel_asset.as_posix()

    # diagnostics harness convention:
    # tests/diagnostics/<name>.golden is owned by tests/diagnostics/<name>.curlee
    if rel.startswith("tests/diagnostics/") and rel_asset.suffix == ".golden":
        sibling = rel_asset.with_suffix(".curlee")
        return (repo_root / sibling).exists()

    return False


def find_orphan_assets(repo_root: Path) -> tuple[list[str], dict[str, Any]]:
    test_sources = load_test_sources(repo_root)
    source_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore") for path in test_sources
    )

    explicit_refs = collect_explicit_asset_refs(source_text)

    assets: list[Path] = sorted(
        path.relative_to(repo_root)
        for path in (repo_root / "tests").rglob("*")
        if path.is_file() and path.suffix in (".golden", ".expected")
    )

    orphans: list[str] = []
    for rel_asset in assets:
        rel_text = rel_asset.as_posix()
        basename = rel_asset.name

        explicit_match = rel_text in explicit_refs or basename in explicit_refs
        implicit_match = is_implicitly_owned(repo_root, rel_asset)

        if not explicit_match and not implicit_match:
            orphans.append(rel_text)

    summary = {
        "assets_scanned": len(assets),
        "sources_scanned": len(test_sources),
        "orphans": len(orphans),
    }
    return sorted(orphans), summary


def render_text(orphans: list[str], summary: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append("Orphan golden/expected check")
    lines.append("============================")
    lines.append(f"assets_scanned: {summary['assets_scanned']}")
    lines.append(f"sources_scanned: {summary['sources_scanned']}")
    lines.append(f"orphans: {summary['orphans']}")
    lines.append("")

    if not orphans:
        lines.append("OK: no orphan golden/expected files found")
        return "\n".join(lines)

    lines.append("Orphan assets:")
    for path in orphans:
        lines.append(f"- {path}")

    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()

    orphans, summary = find_orphan_assets(repo_root)

    if args.format == "json":
        payload = {"summary": summary, "orphans": orphans}
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(render_text(orphans, summary))

    return 1 if orphans else 0


if __name__ == "__main__":
    raise SystemExit(main())
