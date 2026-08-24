/* SPDX-License-Identifier: MIT
 *
 * curlee x86-64 kernel linker script (linker.ld)
 *
 * Used by `curlee build --link` to produce a bootable kernel ELF. The image is
 * loaded by a multiboot2-capable bootloader (or qemu -kernel) at 0x100000.
 *
 * Layout conventions:
 *   - .text/.rodata/.data/.bss at the multiboot base 0x100000.
 *   - .bss is followed by a 16 KiB stack region whose top is exported as
 *     `__stack_top` (crt0.S sets %rsp to it).
 *   - `__bss_start` / `__bss_end` bracket the zero-initialized data that
 *     crt0.S clears before calling curlee_main.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : {
        KEEP(*(.multiboot2))
        *(.text .text.*)
    }

    .note.Xen : {
        KEEP(*(.note.Xen))
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
        . = ALIGN(16);
    }

    /* Static stack: 16 KiB of reserved zero-initialized storage. */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 16K;
        __stack_top = .;
    }
}
