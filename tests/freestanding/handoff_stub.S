// SPDX-License-Identifier: MIT
//
// Issue #297 external-linkage handoff — hand-written assembly stub.
//
// Mirrors what joeos's kernel/boot.S does: deposit ONE raw value into the
// externally-linkable Curlee static BEFORE any Curlee code (including
// curlee_main) runs. `extern static boot_value: U64 = 0;` in the fixture
// codegens to `uint64_t boot_value = 0;` — a plain file-scope global with
// EXTERNAL linkage (no `static` keyword, verbatim identifier) — so this
// assembly can reference and write the symbol directly. The hosted driver
// calls boot_deposit() before curlee_main(), reproducing the boot.S ->
// curlee_main ordering in a normal host executable; the qemu boot smoke
// (handoff_crt0.S) exercises the same write inside a real freestanding boot.
    .text
    .global boot_deposit
    .type boot_deposit, @function
boot_deposit:
    movabsq $0xCAFE, %rax
    movabsq $boot_value, %rdx
    movq %rax, (%rdx)
    ret
    .size boot_deposit, . - boot_deposit
    .section .note.GNU-stack, "", @progbits
