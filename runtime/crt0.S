// SPDX-License-Identifier: MIT
//
// curlee x86-64 boot stub (crt0.S)
//
// This is the entry point for a curlee kernel built with `curlee build --link`.
// It replaces libc's crt0 entirely: no libc, no CRT, no startup dependencies.
//
// Responsibilities, in order:
//   1. Zero the .bss section (symbols __bss_start / __bss_end from linker.ld).
//   2. Set the stack pointer to __stack_top (a static .bss-reserved region).
//   3. Call curlee_main (the codegen entry point; see codegen.cpp).
//   4. On return, disable interrupts and halt forever (deterministic exit).
//
// An optional multiboot2 header (magic 0xE85250D6) is included for GRUB.
//
// QEMU's `-kernel` loader does NOT support multiboot2: it requires a PVH ELF
// note (XEN_ELFNOTE_PHYS32_ENTRY) to boot an uncompressed 64-bit ELF. The
// .note.Xen section below supplies that note (see docs/specs/pvh.rst in QEMU);
// QEMU enters the kernel in 64-bit long mode with identity-mapped paging, so
// the plain .code64 code below runs directly.

.code64
// PVH ELF note: XEN_ELFNOTE_PHYS32_ENTRY (type 18), entry = _start.
.section .note.Xen, "a", @note
.balign 4
.long 2f - 1f                  // namesz
.long 4f - 3f                  // descsz
.long 18                       // XEN_ELFNOTE_PHYS32_ENTRY
1:
.asciz "Xen"
2:
.balign 4
3:
.long _start                   // physical entry point (< 4 GiB)
4:
.balign 4

.section .multiboot2, "a"
.align 8
multiboot2_header:
    .long 0xE85250D6              // magic: multiboot2
    .long 0                       // architecture: i386 (protected mode, 32-bit)
    .long multiboot2_header_end - multiboot2_header
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) // checksum
    // End tag.
    .word 0                       // type: end
    .word 0                       // flags
    .long 8                       // size
multiboot2_header_end:

.section .text
.global _start
.type _start, @function
_start:
    // 1. Zero .bss. __bss_start / __bss_end are absolute symbols provided by
    //    linker.ld; the size is computed at link time.
    movabs $__bss_start, %rax
    movabs $__bss_end, %rcx
    sub %rax, %rcx                // rcx = bss size in bytes
    xor %al, %al                  // fill byte: 0
    // Simple byte loop (bss is small; a rep stosb would need `cld` first).
1:
    test %rcx, %rcx
    jz 2f
    movb %al, (%rax)
    inc %rax
    dec %rcx
    jmp 1b
2:
    // 2. Set stack pointer to the top of the reserved stack region.
    movabs $__stack_top, %rsp
    xor %rbp, %rbp                // clear frame pointer (zero-initialized frame chain)

    // 3. Call the verified entry point.
    call curlee_main

    // 4. Deterministic exit: disable interrupts and halt forever. The loop is
    //    intentional: an NMI can wake the CPU, so re-issue cli; hlt forever.
    cli
3:
    hlt
    jmp 3b

.size _start, . - _start
.section .note.GNU-stack, "", @progbits
