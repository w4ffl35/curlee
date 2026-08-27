# SPDX-License-Identifier: MIT
# Issue #297 external-linkage handoff smoke test.
#
# Usage:
#   cmake -Dcurlee_binary=<path to curlee>
#         -Dcurlee_cc=<C compiler, e.g. gcc>
#         -Dcurlee_as=<assembler, e.g. cc or gcc (assembles .S)>
#         -Dcurlee_fixture=<path to tests/codegen/extern_static_handoff.curlee>
#         -Dcurlee_stub=<path to tests/freestanding/handoff_stub.S>
#         -Dcurlee_driver=<path to tests/freestanding/handoff_driver.c>
#         -Dcurlee_workdir=<scratch dir>
#         -P cmake/extern_static_handoff_smoke.cmake
#
# Proves the acceptance criteria for the assembly->Curlee boundary handoff:
#
#   1. `curlee build` emits the extern static WITHOUT the `static` keyword
#      (`uint64_t boot_value = 0;`, external linkage, verbatim name) — checked
#      by asserting the emitted text contains the bare definition and `nm`
#      reports the symbol as a GLOBAL (` g `), not local (` l `);
#   2. a hand-written .S stub (the boot.S analogue) writes a fixed value into
#      the external symbol and is invoked BEFORE the Curlee entry point;
#   3. the Curlee entry point reads the value back via a normal function call
#      and returns it; the hosted driver asserts the returned value.
#
# This is the minimal-fixture verification plan from the issue: no qemu, no
# kernel boot needed (the qemu ordering is covered by the freestanding CI
# step that links handoff_crt0.S). Requires a C toolchain.

if(NOT DEFINED curlee_binary)
  message(FATAL_ERROR "curlee_binary not set")
endif()
if(NOT DEFINED curlee_cc)
  message(FATAL_ERROR "curlee_cc not set")
endif()
if(NOT DEFINED curlee_fixture)
  message(FATAL_ERROR "curlee_fixture not set")
endif()
if(NOT DEFINED curlee_stub)
  message(FATAL_ERROR "curlee_stub not set")
endif()
if(NOT DEFINED curlee_driver)
  message(FATAL_ERROR "curlee_driver not set")
endif()
if(NOT DEFINED curlee_workdir)
  message(FATAL_ERROR "curlee_workdir not set")
endif()

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

set(_c_file "${curlee_workdir}/handoff.c")
file(WRITE "${_c_file}" "${_c_source}")

# The extern static must be emitted WITHOUT `static` (external linkage) and
# under the verbatim identifier. The fixture declares
# `extern static boot_value: U64 = 0;`, so the emitted C must contain the bare
# definition `uint64_t boot_value = 0;` and must NOT contain a `static` form.
if(NOT _c_source MATCHES "(^|\n)uint64_t boot_value = 0;")
  message(FATAL_ERROR
    "emitted C missing external-linkage definition 'uint64_t boot_value = 0;':\n${_c_source}")
endif()
if(_c_source MATCHES "static uint64_t boot_value")
  message(FATAL_ERROR
    "emitted C has internal linkage for the extern static 'boot_value':\n${_c_source}")
endif()

# 2. Compile the emitted C freestanding, assemble the boot stub, compile the
#    hosted driver.
execute_process(
  COMMAND "${curlee_cc}" -ffreestanding -fno-builtin -nostdlib -std=c11
    -mno-sse -mno-sse2 -c "${_c_file}" -o "${curlee_workdir}/handoff.o"
  RESULT_VARIABLE _cc_rc
  OUTPUT_VARIABLE _cc_out
  ERROR_VARIABLE _cc_err
)
if(NOT _cc_rc EQUAL 0)
  message(FATAL_ERROR "freestanding compile of emitted C failed:\n${_cc_out}\n${_cc_err}")
endif()

execute_process(
  COMMAND "${curlee_cc}" -c "${curlee_stub}" -o "${curlee_workdir}/handoff_stub.o"
  RESULT_VARIABLE _as_rc
  OUTPUT_VARIABLE _as_out
  ERROR_VARIABLE _as_err
)
if(NOT _as_rc EQUAL 0)
  message(FATAL_ERROR "assembly of boot stub failed:\n${_as_out}\n${_as_err}")
endif()

execute_process(
  COMMAND "${curlee_cc}" -std=c11 -c "${curlee_driver}" -o "${curlee_workdir}/handoff_driver.o"
  RESULT_VARIABLE _drv_rc
  OUTPUT_VARIABLE _drv_out
  ERROR_VARIABLE _drv_err
)
if(NOT _drv_rc EQUAL 0)
  message(FATAL_ERROR "compile of hosted driver failed:\n${_drv_out}\n${_drv_err}")
endif()

# 3. Verify the emitted object's symbol table: boot_value must be a GLOBAL
#    (external-linkage) symbol, NOT local. GNU nm prints the symbol type
#    letter in UPPERCASE for global symbols (B/D/T/R...) and lowercase for
#    local ones (b/d/t/r...). `uint64_t boot_value = 0;` is a zero-initialized
#    file-scope global, so it appears as `B` (.bss). This is acceptance
#    criterion (b) — the whole point of the feature.
find_program(_nm NAMES nm)
if(NOT _nm)
  message(FATAL_ERROR "nm not found (binutils required)")
endif()
execute_process(
  COMMAND "${_nm}" "${curlee_workdir}/handoff.o"
  RESULT_VARIABLE _nm_rc
  OUTPUT_VARIABLE _nm_out
  ERROR_VARIABLE _nm_err
)
if(NOT _nm_rc EQUAL 0)
  message(FATAL_ERROR "nm on emitted object failed:\n${_nm_err}")
endif()
if(NOT _nm_out MATCHES "(^|\n)[0-9a-fA-F]+ [BDTR] boot_value($|\n)")
  message(FATAL_ERROR
    "boot_value is not a global symbol in the emitted object (must be external "
    "linkage for boot.S to write it):\n${_nm_out}")
endif()
if(_nm_out MATCHES "(^|\n)[0-9a-fA-F]+ [bdtr] boot_value($|\n)")
  message(FATAL_ERROR "boot_value has local (static) linkage:\n${_nm_out}")
endif()

# 4. Link the hosted test executable and run it. The driver calls boot_deposit
#    (the .S stub) BEFORE curlee_main — the boot.S ordering — and asserts the
#    read-back value.
execute_process(
  COMMAND "${curlee_cc}" -std=c11 "${curlee_workdir}/handoff_driver.o"
    "${curlee_workdir}/handoff.o" "${curlee_workdir}/handoff_stub.o"
    -o "${curlee_workdir}/handoff_test"
  RESULT_VARIABLE _link_rc
  OUTPUT_VARIABLE _link_out
  ERROR_VARIABLE _link_err
)
if(NOT _link_rc EQUAL 0)
  message(FATAL_ERROR "link of handoff test failed:\n${_link_out}\n${_link_err}")
endif()

execute_process(
  COMMAND "${curlee_workdir}/handoff_test"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE _run_err
)
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR
    "handoff test failed (rc=${_run_rc}):\n${_run_out}\n${_run_err}")
endif()

message(STATUS "extern-static handoff OK: ${_run_out}")
