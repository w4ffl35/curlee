#!/usr/bin/env bash
set -euo pipefail

preset="linux-debug"
build_first=1
soak_iterations=300
soak_fuel_failures=150

usage() {
  cat <<'EOF'
Usage: scripts/rc_validation.sh [--preset <cmake-preset>] [--skip-build] [--soak-iterations <n>] [--soak-failures <n>]

Runs RC validation campaign steps for GA confidence:
- focused fuzz regression suite
- deterministic soak runs (success + out-of-fuel)
- pilot app end-to-end (check/run/bundle build/verify/run)
EOF
}

run_cmd() {
  local label="$1"; shift
  echo "[run] $label"
  "$@"
}

expect_ok() {
  local label="$1"; shift
  local tmp
  tmp=$(mktemp)
  if "$@" >"$tmp" 2>&1; then
    rm -f "$tmp"
    echo "[ok]  $label"
    return 0
  fi

  echo "[FAIL] $label (expected success)"
  sed -n '1,200p' "$tmp" || true
  rm -f "$tmp"
  return 1
}

expect_fail_contains() {
  local label="$1"
  local needle="$2"
  shift 2
  local tmp
  tmp=$(mktemp)

  if "$@" >"$tmp" 2>&1; then
    echo "[FAIL] $label (expected failure)"
    sed -n '1,200p' "$tmp" || true
    rm -f "$tmp"
    return 1
  fi

  if ! grep -q "$needle" "$tmp"; then
    echo "[FAIL] $label (missing expected marker: $needle)"
    sed -n '1,200p' "$tmp" || true
    rm -f "$tmp"
    return 1
  fi

  rm -f "$tmp"
  echo "[ok]  $label (failed as expected)"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --skip-build)
      build_first=0
      shift
      ;;
    --soak-iterations)
      soak_iterations="$2"
      shift 2
      ;;
    --soak-failures)
      soak_fuel_failures="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1"
      usage
      exit 2
      ;;
  esac
done

if [[ $build_first -eq 1 ]]; then
  expect_ok "cmake configure (${preset})" cmake --preset "$preset"
  expect_ok "cmake build (${preset})" cmake --build --preset "$preset"
fi

bin="./build/${preset}/curlee"
if [[ ! -x "$bin" ]]; then
  echo "[FAIL] missing executable: $bin"
  exit 1
fi

expect_ok "fuzz regression suite" ctest --preset "$preset" -R curlee_fuzz_regression_tests --output-on-failure

for ((i = 1; i <= soak_iterations; ++i)); do
  expect_ok "soak pass #${i}" "$bin" run tests/fixtures/run_success.curlee
  expect_ok "soak pass profile #${i}" "$bin" run --profile-format=json tests/fixtures/run_success.curlee

done

for ((i = 1; i <= soak_fuel_failures; ++i)); do
  expect_fail_contains "soak out-of-fuel #${i}" "out of fuel" "$bin" run --fuel 10 tests/fixtures/run_infinite_loop.curlee
  expect_fail_contains "soak out-of-fuel profile #${i}" "\"kind\":\"profile\"" "$bin" run --profile-format=json --fuel 10 tests/fixtures/run_infinite_loop.curlee

done

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
bundle_path="$tmp_dir/pilot.bundle"
pilot="examples/pilot_budget_gate.curlee"

expect_ok "pilot check" "$bin" check "$pilot"
expect_ok "pilot run" "$bin" run "$pilot"
expect_ok "pilot bundle build" "$bin" bundle build "$pilot" "$bundle_path"
expect_ok "pilot bundle verify" "$bin" bundle verify "$bundle_path"
expect_ok "pilot bundle run" "$bin" run --bundle "$bundle_path" "$pilot"

echo "[ok]  rc validation campaign passed"
