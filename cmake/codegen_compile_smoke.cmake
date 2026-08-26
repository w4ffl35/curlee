# SPDX-License-Identifier: MIT
# Compile-smoke test for the freestanding C codegen target.
#
# Usage:
#   cmake -Dcurlee_binary=<path to curlee>
#         -Dcurlee_fixture=<path to .curlee fixture>
#         -Dcurlee_cc=<freestanding C compiler, e.g. gcc>
#         -Dcurlee_workdir=<scratch dir for output .c>
#         -P cmake/codegen_compile_smoke.cmake
#
# Runs `curlee build -o - <fixture>`, writes the emitted C to a temp file,
# then compiles it with `gcc -ffreestanding -fno-builtin -nostdlib -c`.
# A successful freestanding compile is itself the primary test: it proves the
# emitted C depends only on freestanding headers (no libc, no libgcc).

if(NOT DEFINED curlee_binary)
  message(FATAL_ERROR "curlee_binary not set")
endif()
if(NOT DEFINED curlee_fixture)
  message(FATAL_ERROR "curlee_fixture not set")
endif()
if(NOT DEFINED curlee_cc)
  message(FATAL_ERROR "curlee_cc not set")
endif()
if(NOT DEFINED curlee_workdir)
  message(FATAL_ERROR "curlee_workdir not set")
endif()

get_filename_component(_base "${curlee_fixture}" NAME_WE)
set(_out_file "${curlee_workdir}/curlee_codegen_${_base}.c")

# 1. Emit C from the verified fixture.
execute_process(
  COMMAND "${curlee_binary}" build -o - "${curlee_fixture}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _c_source
  ERROR_VARIABLE _build_err
)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "curlee build failed for ${curlee_fixture}:\n${_build_err}")
endif()

file(WRITE "${_out_file}" "${_c_source}")

# 2. Compile the emitted C freestanding under strict C11. `-pedantic` turns
#    GNU extensions (empty unions, empty structs, void members) into errors so
#    any extension-dependent emission is a hard failure. -mno-sse -mno-sse2
#    mirror the `curlee build --link` flags: the boot stub never enables SSE
#    (CR4.OSFXSR), and the emitted scalar C never needs it — but gcc would
#    otherwise emit SSE for 16-byte-aligned `{0}` array zero-fills (issue
#    #278), which would #UD at boot.
execute_process(
  COMMAND "${curlee_cc}" -ffreestanding -fno-builtin -nostdlib -std=c11
    -mno-sse -mno-sse2 -Wall -Werror -pedantic -c "${_out_file}" -o "${_out_file}.o"
  RESULT_VARIABLE _cc_rc
  OUTPUT_VARIABLE _cc_out
  ERROR_VARIABLE _cc_err
)
if(NOT _cc_rc EQUAL 0)
  message(FATAL_ERROR
    "freestanding compile of ${_out_file} failed:\n${_cc_out}\n${_cc_err}")
endif()

message(STATUS "freestanding compile OK: ${curlee_fixture}")
