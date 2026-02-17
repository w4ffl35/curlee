#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

CPP_EXTENSIONS = (".h", ".hpp", ".hh", ".hxx", ".c", ".cc", ".cpp", ".cxx")

DEFAULT_THRESHOLDS = {
    "line_length_soft": 100,
    "line_length_hard": 120,
    "max_file_loc": 600,
    "max_function_loc": 80,
    "max_function_complexity": 15,
    "duplicate_window_size": 6,
    "max_cross_file_duplicate_windows": 40,
}

CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch"}
FUNCTION_HEADER_RE = re.compile(
    r"^\s*(?!if\b|for\b|while\b|switch\b|catch\b|else\b)(?:[\w:<>,~*&\[\]\s]+)"
    r"\b([A-Za-z_][\w:]*)\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[^\{]+)?\{\s*$"
)
TRAILING_WS_RE = re.compile(r"[ \t]+$")
TODO_RE = re.compile(r"\b(TODO|FIXME|XXX)\b")
ISSUE_REF_RE = re.compile(r"#\d+")


@dataclass(slots=True)
class Violation:
    rule: str
    severity: str
    message: str
    line: int | None = None
    details: dict[str, Any] | None = None

    def to_json(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "rule": self.rule,
            "severity": self.severity,
            "message": self.message,
        }
        if self.line is not None:
            out["line"] = self.line
        if self.details:
            out["details"] = self.details
        return out


def run_git_ls_files(repo_root: Path) -> list[Path]:
    cmd = ["git", "ls-files"]
    proc = subprocess.run(cmd, cwd=repo_root, check=True, capture_output=True, text=True)
    files: list[Path] = []
    for line in proc.stdout.splitlines():
        rel = line.strip()
        if not rel:
            continue
        path = repo_root / rel
        if path.suffix.lower() in CPP_EXTENSIONS:
            files.append(path)
    return sorted(files)


def strip_inline_comment(line: str) -> str:
    idx = line.find("//")
    if idx == -1:
        return line
    return line[:idx]


def is_code_line(line: str) -> bool:
    stripped = line.strip()
    if not stripped:
        return False
    if stripped.startswith("//"):
        return False
    return True


def normalize_for_dup(line: str) -> str:
    stripped = strip_inline_comment(line).strip()
    if not stripped:
        return ""
    stripped = re.sub(r"\s+", " ", stripped)
    return stripped


def looks_like_function_header(line: str) -> bool:
    if line.strip().startswith("#"):
        return False
    if line.strip().startswith("//"):
        return False
    return FUNCTION_HEADER_RE.match(line) is not None


def approx_complexity(lines: list[str]) -> int:
    c = 1
    for line in lines:
        code = strip_inline_comment(line)
        for kw in CONTROL_KEYWORDS:
            c += len(re.findall(rf"\b{kw}\b", code))
        c += code.count("&&")
        c += code.count("||")
        c += code.count("?")
    return c


def analyze_file(path: Path, thresholds: dict[str, int]) -> tuple[dict[str, Any], list[Violation], list[str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    violations: list[Violation] = []

    non_empty_non_comment_loc = sum(1 for line in lines if is_code_line(line))

    if non_empty_non_comment_loc > thresholds["max_file_loc"]:
        violations.append(
            Violation(
                rule="file.loc",
                severity="warning",
                message=(
                    f"file has {non_empty_non_comment_loc} code lines; "
                    f"threshold is {thresholds['max_file_loc']}"
                ),
                details={"loc": non_empty_non_comment_loc},
            )
        )

    hard_len_lines: list[int] = []
    soft_len_lines: list[int] = []
    trailing_ws_lines: list[int] = []
    todo_no_issue_lines: list[int] = []

    for i, line in enumerate(lines, start=1):
        length = len(line)
        if length > thresholds["line_length_hard"]:
            hard_len_lines.append(i)
        elif length > thresholds["line_length_soft"]:
            soft_len_lines.append(i)

        if TRAILING_WS_RE.search(line):
            trailing_ws_lines.append(i)

        if TODO_RE.search(line) and not ISSUE_REF_RE.search(line):
            todo_no_issue_lines.append(i)

    if hard_len_lines:
        violations.append(
            Violation(
                rule="line.length.hard",
                severity="error",
                message=(
                    f"{len(hard_len_lines)} line(s) exceed hard limit "
                    f"{thresholds['line_length_hard']}"
                ),
                details={"lines": hard_len_lines[:25]},
            )
        )

    if soft_len_lines:
        violations.append(
            Violation(
                rule="line.length.soft",
                severity="warning",
                message=(
                    f"{len(soft_len_lines)} line(s) exceed soft limit "
                    f"{thresholds['line_length_soft']}"
                ),
                details={"lines": soft_len_lines[:25]},
            )
        )

    if trailing_ws_lines:
        violations.append(
            Violation(
                rule="line.trailing_whitespace",
                severity="warning",
                message=f"{len(trailing_ws_lines)} line(s) contain trailing whitespace",
                details={"lines": trailing_ws_lines[:25]},
            )
        )

    if todo_no_issue_lines:
        violations.append(
            Violation(
                rule="comment.todo_without_issue",
                severity="warning",
                message=f"{len(todo_no_issue_lines)} TODO/FIXME marker(s) missing issue reference",
                details={"lines": todo_no_issue_lines[:25]},
            )
        )

    joined = "\n".join(lines)
    if "using namespace std;" in joined:
        violations.append(
            Violation(
                rule="namespace.using_std",
                severity="warning",
                message="avoid 'using namespace std;' in repository code",
            )
        )

    function_metrics: list[dict[str, Any]] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not looks_like_function_header(line):
            i += 1
            continue

        start_line = i + 1
        brace_balance = line.count("{") - line.count("}")
        j = i + 1
        while j < len(lines) and brace_balance > 0:
            brace_balance += lines[j].count("{") - lines[j].count("}")
            j += 1

        end_line = j
        block = lines[i:j]
        if not block:
            i += 1
            continue

        loc = sum(1 for ln in block if is_code_line(ln))
        complexity = approx_complexity(block)

        function_metrics.append(
            {
                "start_line": start_line,
                "end_line": end_line,
                "loc": loc,
                "complexity": complexity,
            }
        )

        if loc > thresholds["max_function_loc"]:
            violations.append(
                Violation(
                    rule="function.loc",
                    severity="warning",
                    message=(
                        f"function at line {start_line} has {loc} code lines; "
                        f"threshold is {thresholds['max_function_loc']}"
                    ),
                    line=start_line,
                    details={"loc": loc, "end_line": end_line},
                )
            )

        if complexity > thresholds["max_function_complexity"]:
            violations.append(
                Violation(
                    rule="function.complexity",
                    severity="warning",
                    message=(
                        f"function at line {start_line} has complexity {complexity}; "
                        f"threshold is {thresholds['max_function_complexity']}"
                    ),
                    line=start_line,
                    details={"complexity": complexity, "end_line": end_line},
                )
            )

        i = max(j, i + 1)

    normalized_for_dup = [normalize_for_dup(line) for line in lines]

    metrics = {
        "loc": non_empty_non_comment_loc,
        "line_count": len(lines),
        "functions": function_metrics,
        "hard_length_count": len(hard_len_lines),
        "soft_length_count": len(soft_len_lines),
    }

    return metrics, violations, normalized_for_dup


def build_duplicate_windows(
    file_norm_lines: dict[str, list[str]], window_size: int
) -> dict[str, list[dict[str, Any]]]:
    window_map: dict[str, dict[str, list[int]]] = defaultdict(lambda: defaultdict(list))

    for path, norm_lines in file_norm_lines.items():
        seq = [ln for ln in norm_lines if ln and not ln.startswith("#include")]
        if len(seq) < window_size:
            continue
        for idx in range(0, len(seq) - window_size + 1):
            window = seq[idx : idx + window_size]
            joined = "\n".join(window)
            if len(joined) < 80:
                continue
            window_map[joined][path].append(idx + 1)

    duplicates_by_file: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for window, paths in window_map.items():
        if len(paths) < 2:
            continue
        sorted_paths = sorted(paths.keys())
        for path in sorted_paths:
            other_paths = [p for p in sorted_paths if p != path]
            duplicates_by_file[path].append(
                {
                    "other_files": other_paths[:5],
                    "sample_line": paths[path][0],
                    "sample": window.splitlines(),
                }
            )

    return duplicates_by_file


def to_text(report: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append("Curlee Quality Audit Report")
    lines.append("=" * 28)
    lines.append("")
    summary = report["summary"]
    lines.append(f"Files scanned: {summary['files_scanned']}")
    lines.append(f"Files with violations: {summary['files_with_violations']}")
    lines.append(f"Total violations: {summary['total_violations']}")
    lines.append("")

    for file_entry in report["files"]:
        if not file_entry["violations"]:
            continue
        lines.append(file_entry["path"])
        lines.append("-" * len(file_entry["path"]))
        for v in file_entry["violations"]:
            line_part = f" (line {v['line']})" if "line" in v else ""
            lines.append(f"- [{v['severity']}] {v['rule']}{line_part}: {v['message']}")
        lines.append("")

    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run deterministic Curlee C++ quality audit.")
    parser.add_argument("--repo-root", default=".", help="Repository root path")
    parser.add_argument("--format", choices=["json", "text"], default="json")
    parser.add_argument("--out", default="", help="Write output to file path")
    parser.add_argument("--fail-on-violations", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    thresholds = dict(DEFAULT_THRESHOLDS)

    files = run_git_ls_files(repo_root)

    file_entries: list[dict[str, Any]] = []
    file_norm_lines: dict[str, list[str]] = {}
    violations_by_rule: Counter[str] = Counter()

    for path in files:
        rel_path = path.relative_to(repo_root).as_posix()
        metrics, violations, norm_lines = analyze_file(path, thresholds)
        file_norm_lines[rel_path] = norm_lines

        file_entries.append(
            {
                "path": rel_path,
                "metrics": metrics,
                "violations": [v.to_json() for v in violations],
            }
        )

        for v in violations:
            violations_by_rule[v.rule] += 1

    duplicates = build_duplicate_windows(file_norm_lines, thresholds["duplicate_window_size"])
    for entry in file_entries:
        rel_path = entry["path"]
        dup_records = duplicates.get(rel_path, [])
        if len(dup_records) > thresholds["max_cross_file_duplicate_windows"]:
            violation = Violation(
                rule="dry.cross_file_duplicate_windows",
                severity="warning",
                message=(
                    f"detected {len(dup_records)} duplicated code windows across files; "
                    f"threshold is {thresholds['max_cross_file_duplicate_windows']}"
                ),
                details={
                    "count": len(dup_records),
                    "samples": dup_records[:5],
                },
            )
            entry["violations"].append(violation.to_json())
            violations_by_rule[violation.rule] += 1

    file_entries.sort(key=lambda x: x["path"])

    files_with_violations = sum(1 for e in file_entries if e["violations"])
    total_violations = sum(len(e["violations"]) for e in file_entries)

    report = {
        "repo_root": repo_root.as_posix(),
        "thresholds": thresholds,
        "summary": {
            "files_scanned": len(file_entries),
            "files_with_violations": files_with_violations,
            "total_violations": total_violations,
            "violations_by_rule": dict(sorted(violations_by_rule.items())),
        },
        "files": file_entries,
    }

    if args.format == "json":
        rendered = json.dumps(report, indent=2, sort_keys=True)
    else:
        rendered = to_text(report)

    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = repo_root / out_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(rendered + "\n", encoding="utf-8")
    else:
        print(rendered)

    if args.fail_on_violations and total_violations > 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
