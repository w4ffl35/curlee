#!/usr/bin/env bash
set -euo pipefail

preset="linux-debug-coverage"
# Keep this as an incremental ratchet rather than an aspirational goal.
# Raise over time as coverage improves.
fail_under="94"
fail_under_branch=""
fail_under_function=""
out_dir="build/coverage"
exclude_throw_branches=1
exclude_unreachable_branches=1

usage() {
  cat <<'EOF'
Usage: scripts/coverage.sh [--preset <cmake-preset>] [--out <dir>] [--fail-under <percent>] [--no-fail]
                           [--fail-under-branch <percent>] [--fail-under-function <percent>]
                           [--include-throw-branches] [--include-unreachable-branches]

Builds + runs tests under coverage instrumentation, then prints a coverage summary.

Defaults:
  --preset      linux-debug-coverage
  --out         build/coverage
  --fail-under  94

Notes:
  --fail-under is line coverage only.
  Branch/function coverage thresholds are optional and only enforced when provided.

Requires one of:
  - gcovr (recommended), or
  - lcov + genhtml

Examples:
  bash scripts/coverage.sh
  bash scripts/coverage.sh --fail-under 95
  bash scripts/coverage.sh --no-fail
  bash scripts/coverage.sh --include-throw-branches
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
    --fail-under-branch)
      fail_under_branch="$2"; shift 2 ;;
    --fail-under-function)
      fail_under_function="$2"; shift 2 ;;
    --no-fail)
      no_fail=1; shift ;;
    --include-throw-branches)
      exclude_throw_branches=0; shift ;;
    --include-unreachable-branches)
      exclude_unreachable_branches=0; shift ;;
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

# Ensure we don't pick up stale profiling data from previous builds/tests.
# This avoids gcov/gcovr failures on old .gcda files for targets that no longer exist
# or whose sources moved.
build_dir="$repo_root/build/${preset}"
find "$build_dir" -name '*.gcda' -delete 2>/dev/null || true
find "$build_dir" -name '*.gcov' -delete 2>/dev/null || true

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
  # build_dir defined above

  # CMake compiler-id objects can emit .gcno/.gcda files that don't map back to
  # stable sources, which causes noisy gcovr warnings/errors.
  find "$build_dir/CMakeFiles" -path '*/CompilerIdCXX/*' \( -name '*.gcno' -o -name '*.gcda' \) -delete 2>/dev/null || true

  # Clean up stale test targets that were removed/renamed, but can leave behind
  # CMakeFiles/*_extra_tests.dir artifacts. These can reference non-existent
  # sources and cause GCOV to emit noisy errors.
  while IFS= read -r -d '' d; do
    target="$(basename "$d" .dir)"
    # Only remove truly stale targets. Real ones will have a corresponding
    # executable in the build directory and should contribute coverage.
    if [[ ! -f "$build_dir/$target" && ! -f "$build_dir/$target.exe" ]]; then
      rm -rf "$d" 2>/dev/null || true
    fi
  done < <(find "$build_dir/CMakeFiles" -maxdepth 2 -type d -name '*_extra_tests.dir' -print0 2>/dev/null || true)

  args=(
    --root "$repo_root"
    --object-directory "$build_dir"
    # Focus on production code coverage (tests are just coverage drivers).
    --filter '^src/'
    --filter '^include/'
    # Exclude build artifacts (including CMake compiler-id objects that gcovr can
    # fail to resolve back to sources).
    --exclude '^build/'
    --exclude '^tests/'
    --exclude '^examples/'
    --exclude '^scripts/'
    --exclude '^cmake/'
    --exclude '^wiki/'
    --exclude ".*${build_dir}/CMakeFiles/.*"
    --gcov-ignore-errors no_working_dir_found
    --print-summary
    --txt
    --txt-metric branch
    --output "$out_dir/coverage.txt"
    --html-details "$out_dir/coverage.html"
  )

  if [[ $exclude_throw_branches -eq 1 ]]; then
    args+=(--exclude-throw-branches)
  fi

  if [[ $exclude_unreachable_branches -eq 1 ]]; then
    args+=(--exclude-unreachable-branches)
  fi

  if [[ $no_fail -eq 0 ]]; then
    args+=(--fail-under-line "$fail_under")

    if [[ -n "$fail_under_branch" ]]; then
      args+=(--fail-under-branch "$fail_under_branch")
    fi

    if [[ -n "$fail_under_function" ]]; then
      args+=(--fail-under-function "$fail_under_function")
    fi
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
    # Extract summary percentages and compare.
    line_pct=$(lcov --summary "$out_dir/coverage.filtered.info" | rg -o 'lines\.+: ([0-9]+\.[0-9]+)%' -r '$1' | head -n1 || true)
    func_pct=$(lcov --summary "$out_dir/coverage.filtered.info" | rg -o 'functions\.+: ([0-9]+\.[0-9]+)%' -r '$1' | head -n1 || true)
    branch_pct=$(lcov --summary "$out_dir/coverage.filtered.info" | rg -o 'branches\.+: ([0-9]+\.[0-9]+)%' -r '$1' | head -n1 || true)

    if [[ -n "$line_pct" ]]; then
      awk -v pct="$line_pct" -v min="$fail_under" 'BEGIN { exit !(pct+0 >= min+0) }' || {
        echo "FAIL: line coverage ${line_pct}% < ${fail_under}%" >&2
        exit 1
      }
    fi

    if [[ -n "$fail_under_function" && -n "$func_pct" ]]; then
      awk -v pct="$func_pct" -v min="$fail_under_function" 'BEGIN { exit !(pct+0 >= min+0) }' || {
        echo "FAIL: function coverage ${func_pct}% < ${fail_under_function}%" >&2
        exit 1
      }
    fi

    if [[ -n "$fail_under_branch" && -n "$branch_pct" ]]; then
      awk -v pct="$branch_pct" -v min="$fail_under_branch" 'BEGIN { exit !(pct+0 >= min+0) }' || {
        echo "FAIL: branch coverage ${branch_pct}% < ${fail_under_branch}%" >&2
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
