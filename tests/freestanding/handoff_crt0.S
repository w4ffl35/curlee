// SPDX-License-Identifier: MIT
//
// Issue #297 extern-static handoff — qemu boot smoke stub (handoff_crt0.S)
//
// This is a TEST-ONLY copy of runtime/crt0.S (the real boot stub) with two
// additions that make the extern-static handoff observable in a real boot,
// mirroring what joeos's kernel/boot.S does:
//
//   1. Before calling curlee_main, the stub WRITES a fixed value (0x2A) into
//      the externally-linkable Curlee static `boot_value` — the same
//      write-before-Curlee-runs ordering as boot.S depositing the multiboot2
//      info pointer captured from %ebx. `extern static boot_value: U64 = 0;`
//      codegens to `uint64_t boot_value = 0;` (external linkage, verbatim
//      name), so hand-written assembly can reference it directly.
//   2. After curlee_main returns, the stub reports the RETURN VALUE through
//      QEMU's isa-debug-exit device (iobase 0xf4): writing value V makes qemu
//      exit with status (V << 1) | 1. The fixture's main() returns
//      boot_value, so qemu exiting with status (0x2A << 1) | 1 = 85 proves the
//      deposit happened before Curlee ran AND Curlee read it back through a
//      normal function call — the acceptance criterion, verified over a real
//      boot.
//
// Everything else (multiboot2 header, PVH note, 32->64 mode switch, .bss
// zeroing, GDT, identity page tables) is copied verbatim from crt0.S so the
// stub boots through the same QEMU/GRUB paths as a real `curlee build --link`
// kernel.

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

.section .text
.global _start
.type _start, @function
_start:
    // Detect entry mode: EFER.LMA (MSR 0xC0000080, bit 10).
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
    // Common 64-bit path: set stack, deposit the handoff value, call the
    // entry point, report the read-back, halt forever.
    movabs $__stack_top, %rsp
    xor %rbp, %rbp

    // ---- Issue #297 handoff: write 0x2A into the externally-linkable
    // Curlee static `boot_value` BEFORE curlee_main runs (boot.S analogue:
    // boot.S captures %ebx into mb2_info_addr before the kernel's C). This
    // runs after .bss zeroing above, so the deposit survives.
    movabsq $0x2A, %rax
    movabsq $boot_value, %rdx
    movq %rax, (%rdx)

    call curlee_main

    // Report the read-back through isa-debug-exit (iobase 0xf4): main()
    // returns boot_value == 0x2A, so qemu exits with status
    // (0x2A << 1) | 1 = 85. Any other status means the handoff failed.
    movl %eax, %eax
    outl %eax, $0xF4
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
