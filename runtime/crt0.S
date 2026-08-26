// SPDX-License-Identifier: MIT
//
// curlee x86-64 boot stub (crt0.S)
//
// This is the entry point for a curlee kernel built with `curlee build --link`.
// It replaces libc's crt0 entirely: no libc, no CRT, no startup dependencies.
//
// Responsibilities, in order:
//   1. Detect the boot protocol (see below) and, when entered in 32-bit
//      protected mode, switch to 64-bit long mode.
//   2. Zero the .bss section (symbols __bss_start / __bss_end from linker.ld).
//   3. Set the stack pointer to __stack_top (a static .bss-reserved region).
//   4. Call curlee_main (the codegen entry point; see codegen.cpp).
//   5. On return, disable interrupts and halt forever (deterministic exit).
//
// Entry modes (the SAME 64-bit kernel must boot through either):
//   - QEMU PVH loader: 64-bit long mode with identity-mapped paging already
//     active (EFER.LMA=1). The stub skips the mode switch.
//   - multiboot2 (QEMU/SeaBIOS's `-kernel` path, GRUB): 32-bit protected
//     mode with paging off (EFER.LMA=0). The stub builds identity-mapped
//     2 MiB page tables for the first 4 GiB, enables PAE + EFER.LME +
//     paging, and far-jumps to the 64-bit code segment.
//
// Why the mode switch is required: QEMU's `-kernel` loader detects the
// multiboot2 header at the start of the image and boots via the multiboot2
// protocol, which enters in 32-bit protected mode — even though the image
// also carries a PVH note. In 32-bit mode a 64-bit kernel's REX-prefixed
// instructions decode as legacy instructions (0x48 = `decl %eax`), silently
// corrupting every 64-bit operation (observed as array/scalar values read
// back one less than stored). The mode probe below uses encodings identical
// in 32-bit and 64-bit mode, so the entry bytes are valid for both.

.code32
// Optional multiboot2 header (magic 0xE85250D6) for GRUB / QEMU -kernel.
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
// uses this to boot an uncompressed 64-bit ELF directly in long mode when no
// multiboot2 header is detected.
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
    // Detect entry mode: EFER.LMA (MSR 0xC0000080, bit 10).
    // 1 = long mode active (PVH loader); 0 = 32-bit protected mode (multiboot2).
    mov $0xC0000080, %ecx
    rdmsr
    test $0x400, %eax
    jnz .Lentry_long_mode

    // ---- 32-bit protected-mode entry (multiboot2) ----
    // 1. Zero .bss (no paging yet; 32-bit addressing).
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
    // 2. Build identity page tables: PML4[0] -> PDPT -> pd0..pd3 with 2 MiB
    //    identity entries covering the first 4 GiB.
    movl $pdpt, %eax
    orl $0x3, %eax
    movl %eax, pml4
    movl $pd0, %eax
    orl $0x3, %eax
    movl %eax, pdpt
    movl $pd1, %eax
    orl $0x3, %eax
    movl %eax, pdpt + 8
    movl $pd2, %eax
    orl $0x3, %eax
    movl %eax, pdpt + 16
    movl $pd3, %eax
    orl $0x3, %eax
    movl %eax, pdpt + 24
    movl $pd0, %edi
    movl $0, %ecx
3:
    // PD entries are 8 bytes: the 2 MiB page number in bits 21-47, flags in
    // bits 0-20. The physical address fits the low 32 bits (< 4 GiB), so the
    // high 4 bytes are always 0.
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax            // P | RW | PS (2 MiB page)
    movl %eax, (%edi)          // low 4 bytes
    movl $0, 4(%edi)           // high 4 bytes
    addl $8, %edi
    incl %ecx
    cmpl $2048, %ecx
    jb 3b
    // 3. Enable PAE, EFER.LME, load CR3, enable paging.
    movl %cr4, %eax
    orl $0x20, %eax            // CR4.PAE
    movl %eax, %cr4
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax           // EFER.LME
    wrmsr
    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $0x80000000, %eax      // CR0.PG
    movl %eax, %cr0
    // 4. Load our GDT (64-bit code segment) and far-jump into long mode.
    lgdt gdt_ptr
    ljmp $0x08, $.L64

.code64
.Lentry_long_mode:
    // ---- 64-bit long-mode entry (PVH loader) ----
    // Zero .bss.
    movabs $__bss_start, %rax
    movabs $__bss_end, %rcx
    sub %rax, %rcx
    xor %al, %al
1:
    test %rcx, %rcx
    jz 2f
    movb %al, (%rax)
    inc %rax
    dec %rcx
    jmp 1b
2:
.L64:
    // Common 64-bit path: set stack, call the entry point, halt forever.
    movabs $__stack_top, %rsp
    xor %rbp, %rbp
    call curlee_main
    cli
3:
    hlt
    jmp 3b

.size _start, . - _start

// GDT for the 32->64 far jump: null, 64-bit code, data.
.section .rodata
.balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF     // 64-bit code (L=1, G=1, base 0, limit 4G)
    .quad 0x00CF92000000FFFF     // data (D=1, G=1, base 0, limit 4G)
gdt_end:
gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt

// Identity page tables for the 32->64 transition. Zeroed with .bss, then
// populated by the trampoline; unused by the PVH path.
.section .bss
.balign 4096
pml4: .space 4096
pdpt: .space 4096
pd0:  .space 4096
pd1:  .space 4096
pd2:  .space 4096
pd3:  .space 4096

.section .note.GNU-stack, "", @progbits
