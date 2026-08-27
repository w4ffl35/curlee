// SPDX-License-Identifier: MIT
//
// curlee 32-bit boot stub (crt0_i386.S)
//
// Entry point for a curlee kernel built with `curlee build --link --arch i386`.
// This is the 32-bit counterpart of crt0.S: it replaces libc's crt0 entirely
// (no libc, no CRT, no startup dependencies) for a FULLY 32-bit ELF
// (ELFCLASS32, i386) image.
//
// Why a separate stub: the 64-bit crt0.S switches to long mode, but a kernel
// whose curlee_main is compiled with -m32 must stay in 32-bit protected mode.
// The 32-bit image is used by 32-bit-only boot paths (e.g. GRUB/ISO builds
// whose boot info pointer is only guaranteed in %ebx for 32-bit entries).
//
// Responsibilities, in order:
//   1. Zero the .bss section (symbols __bss_start / __bss_end from linker.ld).
//   2. Set the stack pointer to __stack_top (a static .bss-reserved region).
//   3. Call curlee_main (the codegen entry point; see codegen.cpp).
//   4. On return, disable interrupts and halt forever (deterministic exit).
//
// Entry modes:
//   - multiboot2 (GRUB, and QEMU's multiboot loader): entered in 32-bit
//     protected mode with paging off; %ebx holds the multiboot2 info pointer
//     (a custom boot stub may capture it — see the joeos GRUB path).
//   - QEMU PVH `-kernel` loader: the XEN_ELFNOTE_PHYS32_ENTRY note below is
//     required for qemu to boot an uncompressed ELF via -kernel (multiboot2
//     alone is NOT accepted by the qemu -kernel loader); the loader enters in
//     32-bit protected mode and jumps to _start.

.code32
// Optional multiboot2 header (magic 0xE85250D6) for GRUB.
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

// PVH ELF note: XEN_ELFNOTE_PHYS32_ENTRY (type 18), entry = _start. QEMU
// uses this to boot an uncompressed ELF via -kernel (the multiboot2 header
// alone is not enough — see the header comment).
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

.section .text
.global _start
.type _start, @function
_start:
    // Zero .bss (32-bit addressing, no paging).
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
1:
    testl %ecx, %ecx
    jz 2f
    movb %al, (%edi)
    incl %edi
    decl %ecx
    jmp 1b
2:
    // Set stack, call the entry point, halt forever.
    movl $__stack_top, %esp
    xorl %ebp, %ebp
    call curlee_main
    cli
3:
    hlt
    jmp 3b

.size _start, . - _start

.section .note.GNU-stack, "", @progbits
