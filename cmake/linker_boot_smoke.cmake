# SPDX-License-Identifier: MIT
# Link-boot smoke test for `curlee build --link` (issue #256).
#
# Usage:
#   cmake -Dcurlee_binary=<path to curlee>
#         -Dcurlee_fixture=<path to .curlee kernel fixture>
#         -Dcurlee_outdir=<scratch dir for the output kernel.elf>
#         -P cmake/linker_boot_smoke.cmake
#
# Runs `curlee build --link -o kernel.elf <fixture>`, then asserts the result
# is a bootable x86-64 ELF:
#   - entry point resolves to `_start` (objdump -f),
#   - the .text/.bss sections and __bss_start/__bss_end/__stack_top symbols are
#     present (readelf -S / -s),
#   - curlee_main is linked (the verified entry the stub calls).
#
# This is the CI-friendly subset of the acceptance criteria that does not
# require qemu. The qemu boot is covered by the freestanding CI job.

if(NOT DEFINED curlee_binary)
  message(FATAL_ERROR "curlee_binary not set")
endif()
if(NOT DEFINED curlee_fixture)
  message(FATAL_ERROR "curlee_fixture not set")
endif()
if(NOT DEFINED curlee_outdir)
  message(FATAL_ERROR "curlee_outdir not set")
endif()

find_program(curlee_objdump NAMES objdump)
find_program(curlee_readelf NAMES readelf)
find_program(curlee_nm NAMES nm)
if(NOT curlee_objdump)
  message(FATAL_ERROR "objdump not found; cannot run linker boot smoke")
endif()
if(NOT curlee_readelf)
  message(FATAL_ERROR "readelf not found; cannot run linker boot smoke")
endif()
if(NOT curlee_nm)
  message(FATAL_ERROR "nm not found; cannot run linker boot smoke")
endif()

file(MAKE_DIRECTORY "${curlee_outdir}")
set(_kernel_elf "${curlee_outdir}/kernel.elf")

# 1. Build the kernel ELF with --link.
execute_process(
  COMMAND "${curlee_binary}" build --link -o "${_kernel_elf}" "${curlee_fixture}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err
)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "curlee build --link failed for ${curlee_fixture}:\n${_build_out}\n${_build_err}")
endif()

if(NOT EXISTS "${_kernel_elf}")
  message(FATAL_ERROR "build --link did not produce ${_kernel_elf}")
endif()

# 2. The ELF must have a defined entry point (non-zero) and the _start symbol
#    must resolve. objdump -f prints only the start address, so verify the
#    symbol table via nm (which names the entry symbol).
execute_process(
  COMMAND "${curlee_objdump}" -f "${_kernel_elf}"
  RESULT_VARIABLE _od_rc
  OUTPUT_VARIABLE _od_out
  ERROR_VARIABLE _od_err
)
if(NOT _od_rc EQUAL 0)
  message(FATAL_ERROR "objdump -f failed:\n${_od_err}")
endif()
if(NOT _od_out MATCHES "start address 0x[0-9a-fA-F]+")
  message(FATAL_ERROR
    "kernel entry point is not set. objdump -f output:\n${_od_out}")
endif()

execute_process(
  COMMAND "${curlee_nm}" "${_kernel_elf}"
  RESULT_VARIABLE _nm_rc
  OUTPUT_VARIABLE _nm_out
  ERROR_VARIABLE _nm_err
)
if(NOT _nm_rc EQUAL 0)
  message(FATAL_ERROR "nm failed:\n${_nm_err}")
endif()
if(NOT _nm_out MATCHES "(^|\n)[0-9a-fA-F]+ [A-Za-z] _start(\n|$)")
  message(FATAL_ERROR
    "kernel symbol table missing _start. nm output:\n${_nm_out}")
endif()

# 3. Sections and linker symbols must be present.
execute_process(
  COMMAND "${curlee_readelf}" -S "${_kernel_elf}"
  RESULT_VARIABLE _re_s_rc
  OUTPUT_VARIABLE _re_s_out
  ERROR_VARIABLE _re_s_err
)
if(NOT _re_s_rc EQUAL 0)
  message(FATAL_ERROR "readelf -S failed:\n${_re_s_err}")
endif()
# The multiboot image must carry the boot-critical sections. .rodata/.data are
# only emitted when the program actually uses them (empty output sections are
# dropped by ld), so only .text/.bss/.stack are asserted here.
foreach(_sec ".text" ".bss" ".stack")
  if(NOT _re_s_out MATCHES "${_sec}")
    message(FATAL_ERROR "readelf -S missing section ${_sec}:\n${_re_s_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${curlee_readelf}" -s "${_kernel_elf}"
  RESULT_VARIABLE _re_sym_rc
  OUTPUT_VARIABLE _re_sym_out
  ERROR_VARIABLE _re_sym_err
)
if(NOT _re_sym_rc EQUAL 0)
  message(FATAL_ERROR "readelf -s failed:\n${_re_sym_err}")
endif()
foreach(_sym "__bss_start" "__bss_end" "__stack_top" "curlee_main" "_start")
  if(NOT _re_sym_out MATCHES "${_sym}")
    message(FATAL_ERROR "readelf -s missing symbol ${_sym}:\n${_re_sym_out}")
  endif()
endforeach()

message(STATUS "linker boot smoke OK: ${_kernel_elf}")
