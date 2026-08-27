# SPDX-License-Identifier: MIT
# i386 link-boot smoke test for `curlee build --link --arch i386` (issue #288).
#
# Usage:
#   cmake -Dcurlee_binary=<path to curlee>
#         -Dcurlee_fixture=<path to .curlee kernel fixture using 64-bit Int>
#         -Dcurlee_outdir=<scratch dir for the output ELFs>
#         [-Dcurlee_fixture_u64_only=<path to U64-only fixture (no Int)>]
#         [-Dcurlee_fixture_no64=<path to fixture with no 64-bit types>]
#         -P cmake/i386_link_smoke.cmake
#
# Proves the issue #288 acceptance criteria without qemu:
#   - a 32-bit freestanding kernel using 64-bit arithmetic links via
#     `curlee build --link --arch i386` with NO downstream-supplied helper file:
#     the bundled runtime/libgcc32_helpers.c is compiled and linked
#     automatically, so every libgcc-ABI symbol (__muldi3/__udivdi3/...) is
#     present in the resulting ELF;
#   - the output is a fully 32-bit ELF (ELFCLASS32, Intel 80386) with the
#     boot-critical sections and _start entry;
#   - the 64-bit PVH target (default --arch) is UNAFFECTED: the same fixture
#     linked without --arch contains NONE of the helper symbols;
#   - the 32-bit image has no RWX LOAD segment (the linker.ld .bss page-align
#     keeps .bss/.stack in a separate RW segment, and -fno-pie -fno-pic keeps
#     .got.plt out of .text).
#
# Optional fixtures pin the auto-link gate in cli_impl.ipp from both sides:
#   - curlee_fixture_u64_only: a program using ONLY U64 (no Int). Its emitted
#     C contains uint64_t — and since the token "uint64_t" embeds the
#     substring "int64_t", the gate's `find("int64_t")` check fires and the
#     helpers are linked. (A separate find("uint64_t") clause in the gate is
#     dead code by construction; see the fixture header.) This proves a
#     U64-only kernel links AND boots with the bundled helpers. 32-bit-only:
#     the 64-bit PVH target never links the helpers, so the extern __udivdi3
#     call would be an undefined symbol there.
#   - curlee_fixture_no64: emitted C has neither int64_t nor uint64_t (a
#     kernel whose only extern takes/returns nothing). On i386 the gate must
#     NOT link the helpers, and a stale curlee_libgcc32.o from an earlier
#     i386 build into the same output directory is dropped; x86_64 is
#     unaffected. This is the "no 64-bit types" side of the gate.
#
# The qemu boot (32-bit + 64-bit serial-identical smoke) is covered by the
# freestanding CI job; this is the CI-friendly link-level subset.

if(NOT DEFINED curlee_binary)
  message(FATAL_ERROR "curlee_binary not set")
endif()
if(NOT DEFINED curlee_fixture)
  message(FATAL_ERROR "curlee_fixture not set")
endif()
if(NOT DEFINED curlee_outdir)
  message(FATAL_ERROR "curlee_outdir not set")
endif()

# Optional gate-pinning fixtures (see header comment): U64-only (emitted C
# with uint64_t, which embeds the int64_t substring) and
# no-64-bit-types-at-all.
if(NOT DEFINED curlee_fixture_u64_only)
  set(curlee_fixture_u64_only "")
endif()
if(NOT DEFINED curlee_fixture_no64)
  set(curlee_fixture_no64 "")
endif()

find_program(curlee_readelf NAMES readelf)
find_program(curlee_nm NAMES nm)
find_program(curlee_ld NAMES ld)
if(NOT curlee_readelf)
  message(FATAL_ERROR "readelf not found; cannot run i386 link smoke")
endif()
if(NOT curlee_nm)
  message(FATAL_ERROR "nm not found; cannot run i386 link smoke")
endif()

# The i386 link requires a C toolchain with 32-bit (-m32 / ld -m elf_i386)
# support. Some runners install only the x86-64 libc/gcc runtime, so probe
# with a trivial freestanding compile+link and skip VISIBLY when unavailable
# (same convention as the qemu boot smoke in scripts/freestanding_ci.sh).
find_program(curlee_cc NAMES cc gcc)
if(NOT curlee_cc)
  message(FATAL_ERROR "no C compiler found; cannot run i386 link smoke")
endif()
if(NOT curlee_ld)
  message(FATAL_ERROR "no ld found; cannot run i386 link smoke")
endif()
file(WRITE "${curlee_outdir}/m32_probe.c" "int f(int a, int b) { return a + b; }\n")
execute_process(
  COMMAND "${curlee_cc}" -m32 -fno-pie -fno-pic -ffreestanding -fno-builtin
          -nostdlib -std=c11 -c "${curlee_outdir}/m32_probe.c"
          -o "${curlee_outdir}/m32_probe.o"
  RESULT_VARIABLE _probe_cc
  OUTPUT_VARIABLE _probe_cc_out
  ERROR_VARIABLE _probe_cc_err
)
execute_process(
  COMMAND "${curlee_ld}" -m elf_i386 -nostdlib -static
          "${curlee_outdir}/m32_probe.o" -o "${curlee_outdir}/m32_probe.elf"
  RESULT_VARIABLE _probe_ld
  OUTPUT_VARIABLE _probe_ld_out
  ERROR_VARIABLE _probe_ld_err
)
if(NOT _probe_cc EQUAL 0 OR NOT _probe_ld EQUAL 0)
  message(STATUS "i386 link smoke SKIPPED: C toolchain lacks 32-bit support "
                 "(gcc -m32: ${_probe_cc}, ld -m elf_i386: ${_probe_ld})")
  message(STATUS "  cc output: ${_probe_cc_out}${_probe_cc_err}")
  message(STATUS "  ld output: ${_probe_ld_out}${_probe_ld_err}")
  return()
endif()

file(MAKE_DIRECTORY "${curlee_outdir}")

# Runs `curlee build --link [--arch <arch>] -o <out> <fixture>` and fails with
# the captured output on error.
function(curlee_link_fixture arch elf fixture)
  set(_args build --link -o "${elf}" "${fixture}")
  if(NOT arch STREQUAL "")
    list(INSERT _args 2 --arch "${arch}")
  endif()
  execute_process(
    COMMAND "${curlee_binary}" ${_args}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "curlee build --link ${arch} failed for ${fixture}:\n${_out}\n${_err}")
  endif()
  if(NOT EXISTS "${elf}")
    message(FATAL_ERROR "build --link did not produce ${elf}")
  endif()
endfunction()

# Asserts the ELF class/machine via readelf -h.
function(curlee_check_elf_class elf expect_class expect_machine)
  execute_process(
    COMMAND "${curlee_readelf}" -h "${elf}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "readelf -h failed for ${elf}:\n${_err}")
  endif()
  if(NOT _out MATCHES "Class:[ \t]+${expect_class}")
    message(FATAL_ERROR
      "expected ${expect_class} ELF for ${elf}, got:\n${_out}")
  endif()
  if(NOT _out MATCHES "Machine:[ \t]+${expect_machine}")
    message(FATAL_ERROR
      "expected machine ${expect_machine} for ${elf}, got:\n${_out}")
  endif()
endfunction()

# Asserts which libgcc32 helper symbols are/aren't present via nm.
function(curlee_check_helpers elf expect_present)
  execute_process(
    COMMAND "${curlee_nm}" "${elf}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "nm failed for ${elf}:\n${_err}")
  endif()
  foreach(_sym __muldi3 __udivdi3 __umoddi3 __udivmoddi4 __divdi3 __moddi3
               __negdi2 __ashldi3 __lshrdi3 __ashrdi3 __cmpdi2)
    string(REGEX MATCH "(^|\n)[0-9a-fA-F]+ [A-Za-z] ${_sym}(\n|$)" _found "${_out}")
    if(expect_present AND NOT _found)
      message(FATAL_ERROR "helper symbol ${_sym} missing from ${elf}:\n${_out}")
    endif()
    if(NOT expect_present AND _found)
      message(FATAL_ERROR
        "helper symbol ${_sym} unexpectedly present in ${elf} (64-bit PVH target "
        "must not link the 32-bit helpers):\n${_out}")
    endif()
  endforeach()
endfunction()

# Asserts no RWX LOAD segment via readelf -l.
function(curlee_check_no_rwx elf)
  execute_process(
    COMMAND "${curlee_readelf}" -l "${elf}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "readelf -l failed for ${elf}:\n${_err}")
  endif()
  if(_out MATCHES "LOAD[^\n]* RWE ")
    message(FATAL_ERROR
      "32-bit kernel ${elf} has an RWX LOAD segment (linker.ld / -fno-pie "
      "regression):\n${_out}")
  endif()
endfunction()

set(_elf32 "${curlee_outdir}/kernel32.elf")
set(_elf64 "${curlee_outdir}/kernel64.elf")

# 1. 64-bit-arithmetic fixture, --arch i386: must link with the helpers.
curlee_link_fixture("i386" "${_elf32}" "${curlee_fixture}")
curlee_check_elf_class("${_elf32}" "ELF32" "Intel 80386")
curlee_check_helpers("${_elf32}" TRUE)
curlee_check_no_rwx("${_elf32}")

# 2. Same fixture, default arch (x86_64 PVH): must link WITHOUT the helpers.
curlee_link_fixture("" "${_elf64}" "${curlee_fixture}")
curlee_check_elf_class("${_elf64}" "ELF64" "Advanced Micro Devices X86-64")
curlee_check_helpers("${_elf64}" FALSE)

# 3. U64-only fixture (emitted C has uint64_t, which embeds the int64_t
#    substring the gate checks), --arch i386: the auto-link gate must still
#    link the helpers. 32-bit-only: x86_64 never links the helpers, so the
#    extern __udivdi3 call would be an undefined symbol there (not asserted).
if(NOT curlee_fixture_u64_only STREQUAL "")
  set(_elf32_u64 "${curlee_outdir}/kernel32_u64only.elf")
  curlee_link_fixture("i386" "${_elf32_u64}" "${curlee_fixture_u64_only}")
  curlee_check_elf_class("${_elf32_u64}" "ELF32" "Intel 80386")
  curlee_check_helpers("${_elf32_u64}" TRUE)
  curlee_check_no_rwx("${_elf32_u64}")
endif()

# 4. No-64-bit fixture (neither int64_t nor uint64_t in the emitted C): the
#    gate must NOT link the helpers on i386 (and a stale curlee_libgcc32.o
#    from an earlier i386 build into the same output dir is dropped), and
#    x86_64 is unchanged.
if(NOT curlee_fixture_no64 STREQUAL "")
  set(_elf32_no64 "${curlee_outdir}/kernel32_no64.elf")
  set(_elf64_no64 "${curlee_outdir}/kernel64_no64.elf")
  curlee_link_fixture("i386" "${_elf32_no64}" "${curlee_fixture_no64}")
  curlee_check_elf_class("${_elf32_no64}" "ELF32" "Intel 80386")
  curlee_check_helpers("${_elf32_no64}" FALSE)
  curlee_link_fixture("" "${_elf64_no64}" "${curlee_fixture_no64}")
  curlee_check_elf_class("${_elf64_no64}" "ELF64" "Advanced Micro Devices X86-64")
  curlee_check_helpers("${_elf64_no64}" FALSE)
endif()

message(STATUS "i386 link smoke OK: ${_elf32} (helpers linked), "
               "${_elf64} (no helpers)")
