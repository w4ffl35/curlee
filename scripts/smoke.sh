#!/usr/bin/env bash
set -euo pipefail

preset="linux-debug"
do_build=1
run_release=0
verbose=0

usage() {
  cat <<'EOF'
Usage: scripts/smoke.sh [--preset <cmake-preset>] [--both] [--skip-build] [--verbose]

Runs a small end-to-end smoke suite:
- build (optional)
- CLI --help
- verification-gated failures (expected fail)
- verified program runs (expected pass)
- golden diagnostics test (ctest)

Examples:
  bash scripts/smoke.sh
  bash scripts/smoke.sh --preset linux-release
  bash scripts/smoke.sh --both
  bash scripts/smoke.sh --skip-build
EOF
}

log() {
  printf "%s\n" "$*"
}

run_cmd() {
  local label="$1"; shift
  if [[ $verbose -eq 1 ]]; then
    log "[run] $label: $*"
  else
    log "[run] $label"
  fi
  "$@"
}

expect_ok() {
  local label="$1"; shift
  local tmp
  tmp=$(mktemp)
  if "$@" >"$tmp" 2>&1; then
    rm -f "$tmp"
    log "[ok]  $label"
    return 0
  fi

  log "[FAIL] $label (expected success)"
  sed -n '1,200p' "$tmp" || true
  rm -f "$tmp"
  return 1
}

expect_fail() {
  local label="$1"; shift
  local tmp
  tmp=$(mktemp)

  if "$@" >"$tmp" 2>&1; then
    log "[FAIL] $label (expected failure)"
    sed -n '1,200p' "$tmp" || true
    rm -f "$tmp"
    return 1
  fi

  # Heuristic: ensure it actually produced a diagnostic.
  if ! rg -q "(^|[[:space:]])error:" "$tmp"; then
    log "[FAIL] $label (failed, but no 'error:' in output)"
    sed -n '1,200p' "$tmp" || true
    rm -f "$tmp"
    return 1
  fi

  rm -f "$tmp"
  log "[ok]  $label (failed as expected)"
  return 0
}

run_smoke_for_preset() {
  local cmake_preset="$1"
  local bin="./build/${cmake_preset}/curlee"

  log ""
  log "=== Curlee smoke: preset=${cmake_preset} ==="

  if [[ $do_build -eq 1 ]]; then
    expect_ok "cmake configure (${cmake_preset})" cmake --preset "${cmake_preset}"
    expect_ok "cmake build (${cmake_preset})" cmake --build --preset "${cmake_preset}"
  else
    log "[skip] build step disabled"
  fi

  if [[ ! -x "$bin" ]]; then
    log "[FAIL] curlee binary not found: $bin"
    log "Hint: run 'cmake --build --preset ${cmake_preset}' or omit --skip-build."
    return 1
  fi

  expect_ok "curlee --help" "$bin" --help

  # Expected failures: verifier rejects.
  expect_fail "check: requires failure" "$bin" check tests/fixtures/check_requires_divide.curlee
  expect_fail "check: ensures failure" "$bin" check tests/fixtures/check_ensures_fail.curlee
  expect_fail "run gated by failed ensures" "$bin" run tests/fixtures/check_ensures_fail.curlee

  # Expected success: check + run.
  expect_ok "check: run_success.curlee" "$bin" check tests/fixtures/run_success.curlee
  expect_ok "run: run_success.curlee" "$bin" run tests/fixtures/run_success.curlee

  # Issue #270: bitwise operators and modulo.
  expect_ok "check: check_bitwise_ops.curlee" "$bin" check tests/fixtures/check_bitwise_ops.curlee
  expect_ok "run: check_bitwise_ops.curlee" "$bin" run tests/fixtures/check_bitwise_ops.curlee
  expect_ok "check: check_bitwise_modulo.curlee" "$bin" check tests/fixtures/check_bitwise_modulo.curlee
  expect_ok "run: check_bitwise_modulo.curlee" "$bin" run tests/fixtures/check_bitwise_modulo.curlee
  expect_ok "check: check_bitwise_equivalence.curlee" "$bin" check tests/fixtures/check_bitwise_equivalence.curlee
  expect_ok "run: check_bitwise_equivalence.curlee" "$bin" run tests/fixtures/check_bitwise_equivalence.curlee

  # Issue #274: U8/U16/U32 port reads are inspectable (widening, comparisons,
  # bitwise with Int literals). Freestanding-only: `check` (the VM cannot run
  # port I/O), plus the freestanding codegen fixture below in codegen_tests.
  expect_ok "check: check_unsigned_inspect.curlee" "$bin" check tests/fixtures/check_unsigned_inspect.curlee

  # Issue #277: construct a U64 from an Int or a literal — Int -> U64 widening
  # in `let`/struct fields, U64 literal adaptation, and U64 == U64 comparisons.
  # A literal at or above 2^32 is rejected (out of range) rather than silently
  # truncated; the freestanding codegen fixture (u64_widen) asserts uint64_t
  # storage.
  expect_ok "check: check_u64_widen.curlee" "$bin" check tests/fixtures/check_u64_widen.curlee
  expect_fail "check: check_u64_widen_oob.curlee" "$bin" check tests/fixtures/check_u64_widen_oob.curlee

  # Issue #278: fixed-size mutable arrays ([T; N] + indexed access) for ring
  # buffers / driver state. The passing fixture covers acceptance #1-#4 (the
  # fb.c tool_queue ring pattern, virtio_net.c rx_buf_state bookkeeping, bounded
  # symbolic indices via loop invariants, read-over-write coherence); the
  # failing fixture proves a constant OOB index is rejected, never silently
  # compiled to C. `curlee run` executes arrays too (issue #300): the VM
  # fixtures below exercise local and module-level arrays (the codegen fixture
  # (array) below in codegen_tests covers the freestanding half).
  expect_ok "check: check_array_ring.curlee" "$bin" check tests/fixtures/check_array_ring.curlee
  expect_fail "check: check_array_oob_const.curlee" "$bin" check tests/fixtures/check_array_oob_const.curlee
  expect_fail "check: check_array_oob_unproven.curlee" "$bin" check tests/fixtures/check_array_oob_unproven.curlee
  expect_ok "run: run_array_vm.curlee" "$bin" run tests/fixtures/run_array_vm.curlee
  expect_ok "run: run_static_array_vm.curlee" "$bin" run tests/fixtures/run_static_array_vm.curlee

  # Examples (also expected success).
  expect_ok "check: mvp_run_int" "$bin" check examples/mvp_run_int.curlee
  expect_ok "run: mvp_run_control_flow" "$bin" run examples/mvp_run_control_flow.curlee

  # Golden diagnostics suite is a strong end-to-end oracle for diagnostics stability.
  expect_ok "ctest: curlee_cli_diagnostics_golden_tests" \
    ctest --preset "${cmake_preset}" -R curlee_cli_diagnostics_golden_tests --output-on-failure

  log "[ok]  smoke suite passed (${cmake_preset})"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    --both)
      run_release=1
      shift
      ;;
    --skip-build)
      do_build=0
      shift
      ;;
    --verbose)
      verbose=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      log "Unknown arg: $1"
      usage
      exit 2
      ;;
  esac
done

run_smoke_for_preset "$preset"
if [[ $run_release -eq 1 ]]; then
  run_smoke_for_preset "linux-release"
fi
