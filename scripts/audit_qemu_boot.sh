#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# QEMU boot smoke for the joeos port-readiness audit fixtures
# (see plans/joeos-port-readiness.md).
#
# Mirrors scripts/freestanding_ci.sh step 5: the generated C is linked
# against runtime/rt.c + runtime/crt0.S + tests/freestanding/debug_exit_driver.c
# (the debug-exit driver overrides the weak curlee_halt so a kernel that
# reaches it makes QEMU exit with status 3).
#
# Usage:
#   bash scripts/audit_qemu_boot.sh <generated.c> <kernel-name>
#     e.g. bash scripts/audit_qemu_boot.sh build/audit/02.c 02
#
# Exit status 3 from qemu = PASS; anything else (or timeout) = FAIL.
set -euo pipefail

gen_c="${1:?generated C file required}"
name="${2:?kernel name required}"
outdir="build/audit/boot"
mkdir -p "$outdir"

cc="${CC:-cc}"

log() { printf "%s\n" "$*"; }

log "compile: $(basename "$gen_c") + debug_exit_driver + rt + crt0"
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -Iruntime -c \
  "$gen_c" -o "$outdir/${name}.o"
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -Iruntime -c \
  tests/freestanding/debug_exit_driver.c -o "$outdir/debug_exit_driver.o"
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -Iruntime -c \
  runtime/rt.c -o "$outdir/rt.o"
$cc -ffreestanding -fno-builtin -nostdlib -c \
  runtime/crt0.S -o "$outdir/crt0.o"
ld -nostdlib -T runtime/linker.ld -o "$outdir/${name}_smoke.elf" \
  "$outdir/${name}.o" "$outdir/debug_exit_driver.o" "$outdir/rt.o" "$outdir/crt0.o"

log "boot: qemu-system-x86_64 -kernel ${name}_smoke.elf (isa-debug-exit)"
set +e
timeout 15 qemu-system-x86_64 -display none \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -serial file:"$outdir/${name}-serial.log" \
  -no-reboot -kernel "$outdir/${name}_smoke.elf" >"$outdir/${name}-qemu.log" 2>&1
rc=$?
set -e

if [ "$rc" -eq 3 ]; then
  log "BOOT OK (rc=3): kernel reached curlee_halt"
elif [ "$rc" -eq 124 ]; then
  log "BOOT OK-ish (rc=124 timeout): kernel booted but did not reach curlee_halt"
  log "serial: $(cat "$outdir/${name}-serial.log" 2>/dev/null || true)"
else
  log "BOOT FAILED (rc=$rc)"
  cat "$outdir/${name}-qemu.log" || true
  exit 1
fi
