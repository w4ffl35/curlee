// SPDX-License-Identifier: MIT
//
// Hosted driver for the issue #297 extern-static handoff smoke test.
//
// The codegen fixture (tests/codegen/extern_static_handoff.curlee) declares
// `extern static boot_value: U64 = 0;` and `fn main() -> U64 { return
// boot_value; }`, which codegens to `uint64_t boot_value = 0;` (external
// linkage) plus `uint64_t curlee_main(void)`. This driver:
//
//   1. calls boot_deposit() (hand-written .S, see handoff_stub.S) — the
//      boot.S analogue — which writes 0xCAFE into `boot_value` BEFORE any
//      Curlee code runs;
//   2. calls curlee_main() and asserts it returns 0xCAFE, proving the
//      write-before-Curlee-runs ordering and the external linkage actually
//      work (acceptance criterion 1 + the issue's minimal-fixture plan).
//
// The PVH-style default is covered implicitly: without boot_deposit() the
// definition `uint64_t boot_value = 0;` supplies the sensible default 0.
#include <stdint.h>
#include <stdio.h>

// Provided by handoff_stub.S: writes 0xCAFE into the external `boot_value`
// symbol before curlee_main runs (boot.S analogue).
void boot_deposit(void);

// Provided by the codegen object (extern_static_handoff fixture): the
// definition of `boot_value` plus the Curlee entry point.
extern uint64_t boot_value;
uint64_t curlee_main(void);

int main(void)
{
    boot_deposit(); // runs BEFORE curlee_main, like boot.S before the kernel's C
    const uint64_t v = curlee_main();
    if (v != 0xCAFE)
    {
        fprintf(stderr, "FAIL: curlee_main returned %llu, expected 0xCAFE (51966)\n",
                (unsigned long long)v);
        return 1;
    }
    printf("OK: boot_deposit wrote 0xCAFE, curlee_main read back %llu\n",
           (unsigned long long)v);
    return 0;
}
