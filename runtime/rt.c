// SPDX-License-Identifier: MIT
//
// curlee_rt: minimal freestanding runtime for generated C.
//
// This is deliberately NOT a libc: no malloc, no printf, no filesystem, no
// threads. It is the "you write malloc and printf yourself" contract from the
// freestanding reality made concrete.
//
// The compiler (GCC/Clang) may emit calls to memset/memcpy/memcmp for struct
// copies and zeroing, so these builtins must exist in the freestanding image.
// curlee_halt/curlee_panic give the kernel a defined way to stop, and the weak
// curlee_putc gives early bring-up a way to print without any driver.

#include "rt.h"

void* memset(void* dst, int c, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; ++i)
    {
        d[i] = (unsigned char)c;
    }
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i)
    {
        d[i] = s[i];
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    for (size_t i = 0; i < n; ++i)
    {
        if (pa[i] != pb[i])
        {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

CURLEE_RT_NORETURN void curlee_halt(void)
{
#if defined(__x86_64__) || defined(__i386__)
    // x86-64 (and i386): disable interrupts and halt. The loop is intentional:
    // an NMI (or, before CLI takes effect, an interrupt) can wake the CPU, so
    // we re-issue cli; hlt forever. Volatile asm prevents the compiler from
    // optimizing the loop away.
    for (;;)
    {
        __asm__ volatile("cli; hlt" ::: "memory");
    }
#else
    // Other architectures are future work; spin forever so the symbol still
    // provides a defined, noreturn stop.
    for (;;)
    {
    }
#endif
}

CURLEE_RT_NORETURN void curlee_panic(const char* msg)
{
    // No formatting in this runtime. If a host has overridden curlee_putc we
    // still get a minimal, driver-free way to surface the failure. curlee_putc
    // is a weak symbol, so this is safe even when no driver exists: the default
    // no-op keeps the reference resolvable at link time.
    while (*msg != '\0')
    {
        curlee_putc(*msg);
        ++msg;
    }
    curlee_putc('\n');
    curlee_halt();
}

CURLEE_RT_WEAK void curlee_putc(char c)
{
    (void)c; // default: no-op so linking always succeeds; host overrides.
}
