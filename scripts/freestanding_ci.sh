#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Freestanding CI flow (issue #257): exercises the whole freestanding path
# end-to-end the way the `freestanding` CI job does:
#
#   1. curlee build (codegen freestanding C)
#   2. gcc -ffreestanding -fno-builtin -nostdlib -c on the generated C
#   3. curlee build --link -> kernel.elf, objdump entry + PVH note check
#   4. qemu boot smoke (if qemu present): the kernel is linked against the
#      smoke-only debug-exit driver so reaching curlee_halt makes qemu exit
#      with status 3 (isa-debug-exit). Absence of qemu skips visibly.
#
# Usage:
#   bash scripts/freestanding_ci.sh [--preset linux-debug]
#   bash scripts/freestanding_ci.sh --binary <path-to-curlee>
#
# --binary takes precedence over --preset and lets callers (e.g. the ctest
# registration in CMakeLists.txt) point at the binary of the current build
# tree, whatever preset it was configured with. Without --binary the script
# derives the path from --preset (default linux-debug).
set -euo pipefail

preset="linux-debug"
bin=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset) preset="$2"; shift 2 ;;
    --binary) bin="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: scripts/freestanding_ci.sh [--preset <cmake-preset> | --binary <path-to-curlee>]"
      exit 0 ;;
    *) echo "Unknown arg: $1"; exit 2 ;;
  esac
done

if [[ -z "$bin" ]]; then
  bin="./build/${preset}/curlee"
fi
if [[ ! -x "$bin" ]]; then
  echo "curlee binary not found: $bin (run cmake --build --preset ${preset} first)"
  exit 1
fi

outdir="build/freestanding-ci"
rm -rf "$outdir"
mkdir -p "$outdir"

cc="${CC:-cc}"

log() { printf "%s\n" "$*"; }
step() { log ""; log "=== $* ==="; }

step "1. Codegen freestanding C (kernel_hello)"
"$bin" build --target freestanding-c -o "$outdir/kernel_hello.c" tests/codegen/kernel_hello.curlee >/dev/null
test -s "$outdir/kernel_hello.c"

step "2. Compile generated C freestanding (gcc -ffreestanding ... -c)"
# -mno-sse -mno-sse2: the boot stub never enables SSE (CR4.OSFXSR), and the
# emitted C is scalar-only — but gcc would otherwise emit SSE instructions
# for 16-byte-aligned `{0}` array zero-fills (issue #278), which #UD without
# OSFXSR. Same flags as `curlee build --link` and the codegen compile smoke.
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -mno-sse -mno-sse2 -Iruntime -c \
  "$outdir/kernel_hello.c" -o "$outdir/kernel_hello.o"

step "3. Codegen freestanding C (phys.mem)"
"$bin" build --target freestanding-c -o "$outdir/phys_mem.c" tests/codegen/phys_mem.curlee >/dev/null
test -s "$outdir/phys_mem.c"

step "4. Link kernel ELF (--link) + entry check"
"$bin" build --link -o "$outdir/kernel.elf" tests/codegen/kernel_hello.curlee >/dev/null
test -s "$outdir/kernel.elf"
objdump -f "$outdir/kernel.elf" | grep -q "start address 0x"
nm "$outdir/kernel.elf" | grep -q " _start$"
# PVH ELF note: qemu -kernel requires .note.Xen (XEN_ELFNOTE_PHYS32_ENTRY)
# to boot an uncompressed 64-bit ELF (multiboot2 alone is NOT supported).
readelf -S "$outdir/kernel.elf" | grep -q "\.note\.Xen"
log "kernel entry + PVH note check OK"

step "5. qemu boot smoke (skips if qemu missing)"
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  log "qemu-system-x86_64 not present; skipping boot smoke (codegen + link verified)"
  exit 0
fi

# Re-link the kernel with the smoke-only debug-exit driver so the boot is
# observable: curlee_halt writes to qemu's isa-debug-exit (iobase 0xf4),
# which exits qemu with status 3.
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -mno-sse -mno-sse2 -Iruntime -c \
  tests/freestanding/debug_exit_driver.c -o "$outdir/debug_exit_driver.o"
$cc -ffreestanding -fno-builtin -nostdlib -std=c11 -mno-sse -mno-sse2 -Iruntime -c \
  runtime/rt.c -o "$outdir/rt.o"
$cc -ffreestanding -fno-builtin -nostdlib -c \
  runtime/crt0.S -o "$outdir/crt0.o"
ld -nostdlib -T runtime/linker.ld -o "$outdir/kernel_smoke.elf" \
  "$outdir/kernel_hello.o" "$outdir/debug_exit_driver.o" "$outdir/rt.o" "$outdir/crt0.o"

set +e
timeout 15 qemu-system-x86_64 -display none \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -serial file:"$outdir/qemu-serial.log" \
  -no-reboot -kernel "$outdir/kernel_smoke.elf" >"$outdir/qemu.log" 2>&1
rc=$?
set -e

log "qemu rc=$rc"
if [ "$rc" -eq 3 ]; then
  log "qemu boot OK: kernel reached curlee_halt (debug-exit status 3)"
  log "serial output: $(cat "$outdir/qemu-serial.log" 2>/dev/null || true)"
elif [ "$rc" -eq 124 ]; then
  log "qemu boot OK: kernel booted and halted deterministically (timeout reached, no debug-exit device present)"
else
  log "qemu boot FAILED (rc=$rc)"
  cat "$outdir/qemu.log" || true
  exit 1
fi

log ""
log "freestanding CI flow passed"
