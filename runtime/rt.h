// SPDX-License-Identifier: MIT
//
// curlee_rt: minimal freestanding runtime for generated C.
//
// This is deliberately NOT a libc. It provides:
//   - the compiler builtins (memset/memcpy/memcmp) that GCC/Clang may emit
//     calls to for struct copies and zeroing,
//   - curlee_halt / curlee_panic to stop the machine in a defined way,
//   - a weak curlee_putc hook that a host driver (UEFI, serial, ...) may
//     override.
//
// It must compile in a freestanding environment with zero libc dependencies
// (only freestanding <stddef.h>/<stdint.h>), so it can be included from
// generated C. The same header is also safe to include from C++ (the test
// suite does this), so the mem builtin declarations carry `noexcept` in C++
// mode to match the standard library's contract.

#ifndef CURLEE_RT_H
#define CURLEE_RT_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(__cplusplus)
#define CURLEE_RT_NORETURN [[noreturn]]
#else
#define CURLEE_RT_NORETURN _Noreturn
#endif

#if defined(__cplusplus)
#define CURLEE_RT_NOEXCEPT noexcept
#else
#define CURLEE_RT_NOEXCEPT
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CURLEE_RT_WEAK __attribute__((weak))
#else
#define CURLEE_RT_WEAK
#endif

    void* memset(void* dst, int c, size_t n) CURLEE_RT_NOEXCEPT;
    void* memcpy(void* dst, const void* src, size_t n) CURLEE_RT_NOEXCEPT;
    int memcmp(const void* a, const void* b, size_t n) CURLEE_RT_NOEXCEPT;

    CURLEE_RT_NORETURN void curlee_halt(void);
    CURLEE_RT_NORETURN void curlee_panic(const char* msg);

    CURLEE_RT_WEAK void curlee_putc(char c);

#if defined(__cplusplus)
}
#endif

#endif /* CURLEE_RT_H */
