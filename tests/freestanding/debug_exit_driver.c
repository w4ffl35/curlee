// SPDX-License-Identifier: MIT
//
// Smoke-only driver for the freestanding qemu boot check (issue #257).
//
// Linked into a `curlee build` kernel image ONLY by the CI/verification
// smoke path (never part of `curlee build --link`'s default pipeline). It
// overrides the two weak runtime symbols (see runtime/rt.c) so the boot can
// be observed deterministically:
//
//   - curlee_putc: writes to COM1 via `outb` (port 0x3F8), captured via
//                  `-serial file:`; proves curlee_main actually executed.
//   - curlee_halt: writes to QEMU's isa-debug-exit device (iobase=0xf4) via
//                  `outl`. QEMU exits with status (value << 1) | 1, so
//                  writing 1 exits with status 3, unambiguously proving the
//                  kernel reached curlee_halt (i.e. curlee_main returned).
//
// A crash (e.g. triple fault) makes QEMU exit with a different status, so
// status 3 is a positive assertion that the whole path ran.

// COM1 I/O ports (outb, not memory-mapped).
#define COM1_THR 0x3F8
#define COM1_LCR 0x3FB
#define COM1_LSR 0x3FD

void curlee_putc(char c)
{
    __asm__ volatile("outb %0, %1" ::"a"((unsigned char)0x03), "Nd"((unsigned short)COM1_LCR));
    while (1)
    {
        unsigned char lsr;
        __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"((unsigned short)COM1_LSR));
        if ((lsr & 0x20) != 0)
        {
            break; // THR empty
        }
    }
    __asm__ volatile("outb %0, %1" ::"a"((unsigned char)c), "Nd"((unsigned short)COM1_THR));
}

void curlee_halt(void)
{
    // isa-debug-exit: write value 1 -> QEMU exits with status (1 << 1) | 1 = 3.
    // The device is an I/O port (iobase=0xf4, iosize=0x04), so the write must
    // be an `outl` to port 0xf4 — a memory store to 0xf4 would not reach it.
    for (;;)
    {
        __asm__ volatile("outl %0, %1" ::"a"(0x1u), "Nd"((unsigned short)0xF4));
    }
}
