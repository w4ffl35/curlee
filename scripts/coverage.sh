#!/usr/bin/env bash
set -euo pipefail

preset="linux-debug-coverage"
fail_under="100"
out_dir="build/coverage"

usage() {
  cat <<'EOF'
Usage: scripts/coverage.sh [--preset <cmake-preset>] [--out <dir>] [--fail-under <percent>] [--no-fail]

Builds + runs tests under coverage instrumentation, then prints a coverage summary.

Defaults:
  --preset      linux-debug-coverage
  --out         build/coverage
  --fail-under  100

Requires one of:
  - gcovr (recommended), or
  - lcov + genhtml

Examples:
  bash scripts/coverage.sh
  bash scripts/coverage.sh --fail-under 95
  bash scripts/coverage.sh --no-fail
EOF
}

no_fail=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"; shift 2 ;;
    --out)
      out_dir="$2"; shift 2 ;;
    --fail-under)
      fail_under="$2"; shift 2 ;;
    --no-fail)
      no_fail=1; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown arg: $1" >&2
      usage
      exit 2
      ;;
  esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset" --output-on-failure

mkdir -p "$out_dir"

# Try gcovr first.
if command -v gcovr >/dev/null 2>&1; then
  # Clean previous reports (but keep directory).
  rm -f "$out_dir"/coverage.* || true

  # Notes:
  # - root = repo root
  # - object-directory = preset build dir
  # - exclude build/ and any third_party-style dirs (none currently)
  build_dir="$repo_root/build/${preset}"

  # CMake compiler-id objects can emit .gcno/.gcda files that don't map back to
  # stable sources, which causes noisy gcovr warnings/errors.
  find "$build_dir/CMakeFiles" -path '*/CompilerIdCXX/*' \( -name '*.gcno' -o -name '*.gcda' \) -delete 2>/dev/null || true

  args=(
    --root "$repo_root"
    --object-directory "$build_dir"
    # Exclude build artifacts (including CMake compiler-id objects that gcovr can
    # fail to resolve back to sources).
    --exclude ".*${repo_root}/build/.*"
    --exclude ".*${build_dir}/CMakeFiles/.*"
    --gcov-ignore-errors no_working_dir_found
    --print-summary
    --txt
    --txt-metric branch
    --output "$out_dir/coverage.txt"
    --html-details "$out_dir/coverage.html"
  )

  if [[ $no_fail -eq 0 ]]; then
    args+=(--fail-under-line "$fail_under")
  fi

  gcovr "${args[@]}"

  echo ""
  echo "Coverage report: $out_dir/coverage.html"
  echo "Coverage summary: $out_dir/coverage.txt"
  exit 0
fi

# Fallback: lcov/genhtml
if command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1; then
  build_dir="$repo_root/build/${preset}"

  lcov --capture --directory "$build_dir" --output-file "$out_dir/coverage.info" >/dev/null
  lcov --remove "$out_dir/coverage.info" "/usr/*" "$repo_root/build/*" --output-file "$out_dir/coverage.filtered.info" >/dev/null
  genhtml "$out_dir/coverage.filtered.info" --output-directory "$out_dir/html" >/dev/null

  lcov --summary "$out_dir/coverage.filtered.info" | tee "$out_dir/coverage.txt"

  if [[ $no_fail -eq 0 ]]; then
    # Extract line coverage percentage and compare.
    pct=$(lcov --summary "$out_dir/coverage.filtered.info" | rg -o 'lines\.+: ([0-9]+\.[0-9]+)%' -r '$1' | head -n1 || true)
    if [[ -n "$pct" ]]; then
      awk -v pct="$pct" -v min="$fail_under" 'BEGIN { exit !(pct+0 >= min+0) }' || {
        echo "FAIL: line coverage ${pct}% < ${fail_under}%" >&2
        exit 1
      }
    fi
  fi

  echo ""
  echo "Coverage report: $out_dir/html/index.html"
  echo "Coverage summary: $out_dir/coverage.txt"
  exit 0
fi

echo "Neither gcovr nor (lcov+genhtml) is installed." >&2
echo "Install one of:" >&2
echo "  sudo apt-get update && sudo apt-get install -y gcovr" >&2
echo "  sudo apt-get update && sudo apt-get install -y lcov" >&2
exit 2
