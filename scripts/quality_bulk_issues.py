#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
from pathlib import Path

TITLE_PREFIX = "[quality-refactor]"


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True)


def load_report(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def get_open_issue_titles(repo_root: Path) -> set[str]:
    cmd = ["gh", "issue", "list", "--state", "open", "--limit", "500", "--json", "title"]
    proc = run(cmd, repo_root)
    items = json.loads(proc.stdout)
    return {item["title"] for item in items}


def format_issue_body(path: str, violations: list[dict], parent_issue: int | None) -> str:
    lines: list[str] = []
    lines.append(f"Quality refactor needed for `{path}`.")
    lines.append("")
    if parent_issue is not None:
        lines.append(f"Parent quality campaign: #{parent_issue}")
        lines.append("")

    lines.append("## Findings")
    for v in violations:
        line_part = f" (line {v['line']})" if "line" in v else ""
        lines.append(f"- **{v['rule']}**{line_part}: {v['message']}")

    lines.append("")
    lines.append("## Acceptance Criteria")
    lines.append("- Refactor this file to satisfy repository quality standards.")
    lines.append("- Remove/reduce the listed violations without changing behavior.")
    lines.append("- Add/update targeted tests if behavior-sensitive code is touched.")
    lines.append("- Keep diagnostics and deterministic behavior stable.")

    lines.append("")
    lines.append("## Verification")
    lines.append("- Run targeted tests for this file's subsystem.")
    lines.append("- Run `cmake --build --preset linux-debug`.")
    lines.append("- Run `ctest --preset linux-debug --output-on-failure` (or focused subset first).")

    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bulk create one quality-refactor issue per file.")
    parser.add_argument("--repo-root", default=".", help="Repository root path")
    parser.add_argument("--report", required=True, help="Path to quality report JSON")
    parser.add_argument("--label", action="append", default=["area:quality"], help="Issue label")
    parser.add_argument("--parent-issue", type=int, default=152, help="Parent campaign issue number")
    parser.add_argument("--apply", action="store_true", help="Actually create issues (default is dry-run)")
    parser.add_argument("--max", type=int, default=0, help="Max issues to create (0 = no limit)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    report_path = Path(args.report)
    if not report_path.is_absolute():
        report_path = repo_root / report_path

    report = load_report(report_path)
    files = report.get("files", [])

    candidates: list[tuple[str, list[dict]]] = []
    for entry in files:
        path = entry.get("path", "")
        violations = entry.get("violations", [])
        if not path or not violations:
            continue
        candidates.append((path, violations))
    candidates.sort(key=lambda x: x[0])

    if args.max > 0:
        candidates = candidates[: args.max]

    open_titles = get_open_issue_titles(repo_root)

    created = 0
    skipped_existing = 0

    for path, violations in candidates:
        title = f"{TITLE_PREFIX} {path}"
        if title in open_titles:
            skipped_existing += 1
            continue

        body = format_issue_body(path, violations, args.parent_issue)

        if not args.apply:
            print(f"DRY-RUN create: {title}")
            continue

        cmd = ["gh", "issue", "create", "--title", title, "--body-file", "-"]
        for label in sorted(set(args.label)):
            cmd.extend(["--label", label])

        subprocess.run(cmd, cwd=repo_root, check=True, input=body, text=True)
        created += 1

    mode = "apply" if args.apply else "dry-run"
    print(
        json.dumps(
            {
                "mode": mode,
                "candidates": len(candidates),
                "created": created,
                "skipped_existing": skipped_existing,
            },
            indent=2,
            sort_keys=True,
        )
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
