# SPDX-License-Identifier: MIT
# Freestanding compile-smoke test for the curlee runtime (runtime/rt.c).
#
# Usage:
#   cmake -Dcurlee_cc=<freestanding C compiler, e.g. gcc>
#         -Dcurlee_rt_dir=<directory containing runtime/rt.c and rt.h>
#         -Dcurlee_workdir=<scratch dir>
#         -P cmake/rt_compile_smoke.cmake
#
# Verifies the issue's acceptance criteria that can be checked without a
# real boot stub / linker script (out of scope for this issue):
#   1. rt.c compiles under `gcc -ffreestanding -fno-builtin -nostdlib -c`
#      with the repo's warning policy (-Wall -Wextra -Wpedantic) + -Werror,
#      proving zero libc dependencies.
#   2. A tiny freestanding "kernel" that #includes rt.h compiles under the
#      same flags (rt.h includes cleanly from generated freestanding C).
#   3. The kernel links with -nostdlib WITHOUT defining curlee_putc (weak
#      default lets linking succeed).
#   4. A strong host override of curlee_putc is linked in; objdump confirms
#      the final image resolves to the strong definition (weak symbol dropped).
#   5. objdump -d confirms the x86-64 `cli; hlt` sequence (bytes fa f4) is
#      present in the text (curlee_halt / curlee_panic).

if(NOT DEFINED curlee_cc)
  message(FATAL_ERROR "curlee_cc not set")
endif()
if(NOT DEFINED curlee_rt_dir)
  message(FATAL_ERROR "curlee_rt_dir not set")
endif()
if(NOT DEFINED curlee_workdir)
  message(FATAL_ERROR "curlee_workdir not set")
endif()

file(MAKE_DIRECTORY "${curlee_workdir}")

set(_rt_obj "${curlee_workdir}/rt.o")
set(_kernel_c "${curlee_workdir}/kernel.c")
set(_kernel_obj "${curlee_workdir}/kernel.o")
set(_override_obj "${curlee_workdir}/putc_override.o")
set(_kernel_bin "${curlee_workdir}/kernel.bin")

# 1. Compile the runtime freestanding under the repo's warning policy + -Werror.
execute_process(
  COMMAND "${curlee_cc}" -ffreestanding -fno-builtin -nostdlib -std=c11
    -Wall -Wextra -Wpedantic -Werror -c "${curlee_rt_dir}/rt.c" -o "${_rt_obj}"
  RESULT_VARIABLE _cc_rc
  OUTPUT_VARIABLE _cc_out
  ERROR_VARIABLE _cc_err
)
if(NOT _cc_rc EQUAL 0)
  message(FATAL_ERROR
    "freestanding compile of rt.c failed:\n${_cc_out}\n${_cc_err}")
endif()

# 2. Tiny freestanding "kernel" that #includes rt.h and calls every runtime
#    symbol. It intentionally does NOT define curlee_putc, so the -nostdlib
#    link proves the weak default lets the image link without a driver.
file(WRITE "${_kernel_c}" [[
#include <stdint.h>
#include "rt.h"

void kernel_entry(void)
{
    uint8_t buf[16];
    (void)memset(buf, 0xAB, sizeof(buf));
    (void)memcpy(buf, buf, sizeof(buf));
    (void)memcmp(buf, buf, sizeof(buf));
    curlee_panic("boot failed");
    curlee_halt();
}
]])

execute_process(
  COMMAND "${curlee_cc}" -ffreestanding -fno-builtin -nostdlib -std=c11
    -Wall -Wextra -Wpedantic -Werror -I "${curlee_rt_dir}"
    -c "${_kernel_c}" -o "${_kernel_obj}"
  RESULT_VARIABLE _kcc_rc
  OUTPUT_VARIABLE _kcc_out
  ERROR_VARIABLE _kcc_err
)
if(NOT _kcc_rc EQUAL 0)
  message(FATAL_ERROR
    "freestanding compile of kernel.c (incl. rt.h) failed:\n${_kcc_out}\n${_kcc_err}")
endif()

# 3. Link the runtime + kernel with -nostdlib and a placeholder entry. A real
#    linker script / boot stub is a separate issue; -e kernel_entry is enough
#    to prove the weak putc does not break the link.
execute_process(
  COMMAND "${curlee_cc}" -nostdlib -static -Wl,-e,kernel_entry
    "${_rt_obj}" "${_kernel_obj}" -o "${_kernel_bin}"
  RESULT_VARIABLE _ld_rc
  OUTPUT_VARIABLE _ld_out
  ERROR_VARIABLE _ld_err
)
if(NOT _ld_rc EQUAL 0)
  message(FATAL_ERROR
    "nostdlib link of kernel (weak putc) failed:\n${_ld_out}\n${_ld_err}")
endif()

# 4. Strong host override of curlee_putc; link it in and confirm the weak
#    default is dropped in favor of the strong definition. The override must
#    NOT #include rt.h: the weak attribute on the declaration in rt.h would
#    propagate to this definition and make it weak too. A real host driver
#    (UEFI, serial) declares the symbol itself, exactly like this.
set(_override_c "${curlee_workdir}/putc_override.c")
file(WRITE "${_override_c}" [[
char g_putc_seen[1];
void curlee_putc(char c)
{
    g_putc_seen[0] = c;
}
]])

execute_process(
  COMMAND "${curlee_cc}" -ffreestanding -fno-builtin -nostdlib -std=c11
    -Wall -Wextra -Wpedantic -Werror
    -c "${_override_c}" -o "${_override_obj}"
  RESULT_VARIABLE _occ_rc
  OUTPUT_VARIABLE _occ_out
  ERROR_VARIABLE _occ_err
)
if(NOT _occ_rc EQUAL 0)
  message(FATAL_ERROR
    "freestanding compile of putc_override.c failed:\n${_occ_out}\n${_occ_err}")
endif()

execute_process(
  COMMAND "${curlee_cc}" -nostdlib -static -Wl,-e,kernel_entry
    "${_rt_obj}" "${_kernel_obj}" "${_override_obj}" -o "${_kernel_bin}"
  RESULT_VARIABLE _lod_rc
  OUTPUT_VARIABLE _lod_out
  ERROR_VARIABLE _lod_err
)
if(NOT _lod_rc EQUAL 0)
  message(FATAL_ERROR
    "nostdlib link of kernel (with putc override) failed:\n${_lod_out}\n${_lod_err}")
endif()

# 5. Disassembly assertions.
find_program(_objdump NAMES objdump)
if(NOT _objdump)
  message(FATAL_ERROR "objdump not found; cannot verify hlt sequence")
endif()

execute_process(
  COMMAND "${_objdump}" -d "${_kernel_bin}"
  RESULT_VARIABLE _od_rc
  OUTPUT_VARIABLE _od_out
  ERROR_VARIABLE _od_err
)
if(NOT _od_rc EQUAL 0)
  message(FATAL_ERROR "objdump -d failed:\n${_od_err}")
endif()

string(REGEX MATCHALL "\tfa[ \t]" _matches_cli "${_od_out}")
string(REGEX MATCHALL "\tf4[ \t]" _matches_hlt "${_od_out}")
if(NOT _matches_cli OR NOT _matches_hlt)
  message(FATAL_ERROR
    "cli; hlt sequence (fa f4) not found in kernel disassembly:\n${_od_out}")
endif()

execute_process(
  COMMAND "${_objdump}" -t "${_kernel_bin}"
  RESULT_VARIABLE _odt_rc
  OUTPUT_VARIABLE _odt_out
  ERROR_VARIABLE _odt_err
)
if(NOT _odt_rc EQUAL 0)
  message(FATAL_ERROR "objdump -t failed:\n${_odt_err}")
endif()

# A weak-binding curlee_putc in the final symbol table means the strong host
# override did not win; that is a failure.
if(_odt_out MATCHES "[ \t]w[ \t]+.*curlee_putc")
  message(FATAL_ERROR
    "curlee_putc resolved weak; strong host override did not win:\n${_odt_out}")
endif()
if(NOT _odt_out MATCHES "curlee_putc")
  message(FATAL_ERROR "curlee_putc missing from final symbol table:\n${_odt_out}")
endif()

message(STATUS "rt.c freestanding smoke OK: compile, rt.h include, nostdlib link, cli;hlt, weak putc override")
