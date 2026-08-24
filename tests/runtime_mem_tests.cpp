// SPDX-License-Identifier: MIT
// Host-visible unit tests for the freestanding runtime's compiler builtins.
//
// These exercise the same memset/memcpy/memcmp definitions in runtime/rt.c
// that the freestanding image links, compiled here as a host translation unit
// (the C source is added to this test target and linked directly, so the
// tested code paths are byte-for-byte the freestanding loops). The target is
// built with -fno-builtin so the calls below reach rt.c instead of being
// constant-folded by the compiler.
//
// curlee_halt / curlee_panic / curlee_putc are not exercised here; the
// freestanding smoke (cmake/rt_compile_smoke.cmake) covers the disassembly
// (`cli; hlt`), the weak putc override, and the -nostdlib link.

#include "rt.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static void expect(bool cond, const std::string& msg)
{
    if (!cond)
    {
        fail(msg);
    }
}

static void test_memset_fills_bytes()
{
    std::uint8_t buf[32];
    std::memset(buf, 0x11, sizeof(buf));

    void* ret = memset(buf, 0xAB, sizeof(buf));
    expect(ret == buf, "memset must return dst");
    for (std::size_t i = 0; i < sizeof(buf); ++i)
    {
        if (buf[i] != 0xAB)
        {
            fail("memset did not fill byte " + std::to_string(i));
        }
    }
}

static void test_memset_zero_length_noop()
{
    std::uint8_t buf[4] = {1, 2, 3, 4};
    void* ret = memset(buf, 0xFF, 0);
    expect(ret == buf, "memset(0) must return dst");
    expect(buf[0] == 1 && buf[1] == 2 && buf[2] == 3 && buf[3] == 4,
           "memset(0) must not modify the buffer");
}

static void test_memset_truncates_int_to_byte()
{
    std::uint8_t buf[4] = {0, 0, 0, 0};
    memset(buf, 0x1234, sizeof(buf));
    for (std::uint8_t b : buf)
    {
        expect(b == 0x34, "memset must store (unsigned char)c");
    }
}

static void test_memcpy_copies_exact_bytes()
{
    std::uint8_t src[16];
    std::uint8_t dst[16];
    for (std::size_t i = 0; i < sizeof(src); ++i)
    {
        src[i] = static_cast<std::uint8_t>(i * 3 + 1);
    }
    std::memset(dst, 0, sizeof(dst));

    void* ret = memcpy(dst, src, sizeof(src));
    expect(ret == dst, "memcpy must return dst");
    expect(std::memcmp(dst, src, sizeof(src)) == 0, "memcpy must copy every byte");
}

static void test_memcpy_zero_length_noop()
{
    std::uint8_t dst[4] = {9, 9, 9, 9};
    std::uint8_t src[4] = {1, 2, 3, 4};
    void* ret = memcpy(dst, src, 0);
    expect(ret == dst, "memcpy(0) must return dst");
    for (std::uint8_t b : dst)
    {
        expect(b == 9, "memcpy(0) must not modify the destination");
    }
}

static void test_memcmp_equal()
{
    const std::uint8_t a[] = {1, 2, 3, 4, 5};
    const std::uint8_t b[] = {1, 2, 3, 4, 5};
    expect(memcmp(a, b, sizeof(a)) == 0, "memcmp equal buffers must return 0");
    expect(memcmp(a, b, 0) == 0, "memcmp(0) must return 0");
}

static void test_memcmp_first_difference_wins()
{
    const std::uint8_t a[] = {1, 2, 3, 4, 5};
    const std::uint8_t b[] = {1, 2, 3, 9, 5};
    // Sign of the first differing byte pair: 3 vs 9 => negative.
    expect(memcmp(a, b, sizeof(a)) < 0, "memcmp must report first diff < 0");
    expect(memcmp(b, a, sizeof(a)) > 0, "memcmp must report first diff > 0");
    // Difference beyond n is ignored.
    const std::uint8_t c[] = {1, 2};
    const std::uint8_t d[] = {1, 9};
    expect(memcmp(c, d, 1) == 0, "memcmp must only compare n bytes");
}

static void test_memcmp_unsigned_compare()
{
    // Byte comparison must be unsigned: 0x80 (128) vs 0x00 (0) => positive,
    // even though the raw char values would be negative/positive as signed.
    const std::uint8_t a[] = {0x80};
    const std::uint8_t b[] = {0x00};
    expect(memcmp(a, b, 1) > 0, "memcmp must compare bytes as unsigned");
    expect(memcmp(b, a, 1) < 0, "memcmp must compare bytes as unsigned");
}

int main()
{
    test_memset_fills_bytes();
    test_memset_zero_length_noop();
    test_memset_truncates_int_to_byte();
    test_memcpy_copies_exact_bytes();
    test_memcpy_zero_length_noop();
    test_memcmp_equal();
    test_memcmp_first_difference_wins();
    test_memcmp_unsigned_compare();
    std::cout << "runtime mem tests OK\n";
    return 0;
}
