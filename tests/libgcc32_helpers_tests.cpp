// SPDX-License-Identifier: MIT
//
// Host unit tests for the bundled 32-bit libgcc-ABI helpers
// (runtime/libgcc32_helpers.c, issue #288).
//
// The helpers are compiled into this x86-64 test binary and every one is
// cross-checked against the host's native 64-bit arithmetic over a battery
// of vectors including the edge cases the issue's verification plan lists:
// division by zero, negative operands, and shift counts >= 32 and >= 64.
//
// Cases where native C arithmetic is UB are asserted against the helpers'
// DOCUMENTED semantics instead (which is what the reference implementation
// shipped with and what the acceptance criteria require):
//   - 64-bit divide-by-zero: quotient 0, remainder dividend;
//   - signed division that overflows (INT64_MIN / -1): sign-magnitude wrap
//     (result INT64_MIN), matching the reference and x86 idiv behavior;
//   - shifts with count >= 64: 0 (logical) / sign-fill (arithmetic right).
//
// The vectors also cover the single-word-divisor, two-word-divisor, and
// general restoring-division paths of udivmoddi4_impl — including dividends
// >= 2^32 with a small divisor, the branch that faulted (#DE) in the old
// per-project reference implementation.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
uint64_t __muldi3(uint64_t a, uint64_t b);
uint64_t __udivdi3(uint64_t a, uint64_t b);
uint64_t __umoddi3(uint64_t a, uint64_t b);
uint64_t __udivmoddi4(uint64_t a, uint64_t b, uint64_t* rp);
int64_t __divdi3(int64_t a, int64_t b);
int64_t __moddi3(int64_t a, int64_t b);
int64_t __negdi2(int64_t x);
uint64_t __ashldi3(uint64_t x, int c);
uint64_t __lshrdi3(uint64_t x, int c);
int64_t __ashrdi3(int64_t x, int c);
int64_t __cmpdi2(int64_t a, int64_t b);
}

namespace
{

constexpr uint64_t kUmax = UINT64_MAX;
constexpr int64_t kImax = INT64_MAX;
constexpr int64_t kImin = INT64_MIN;
constexpr uint64_t k2p32 = 1ull << 32;
constexpr uint64_t k2p63 = 1ull << 63;

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void test_mul()
{
    const uint64_t vecs_a[] = {0ull, 1ull, 2ull, k2p32, k2p32 - 1, k2p32 + 1, k2p63,
                               kUmax, (uint64_t)kImax, (uint64_t)kImin, 0xDEADBEEF12345678ull,
                               0x0123456789ABCDEFull};
    const uint64_t vecs_b[] = {0ull, 1ull, 3ull, 7ull, 1000000000ull, k2p32, k2p32 - 1,
                               kUmax, (uint64_t)kImax, 0x100000001ull, 123456789ull};
    for (const uint64_t a : vecs_a)
    {
        for (const uint64_t b : vecs_b)
        {
            CHECK(__muldi3(a, b) == a * b);
        }
    }
}

void test_udiv_umod()
{
    // b != 0: must match native unsigned 64-bit division/modulo exactly.
    const uint64_t vecs_a[] = {0ull, 1ull, 7ull, k2p32, k2p32 - 1, k2p32 + 1, k2p63,
                               kUmax, (uint64_t)kImax, (uint64_t)kImin, 0xDEADBEEF12345678ull};
    const uint64_t vecs_b[] = {1ull, 2ull, 3ull, 7ull, 1000ull, 0x100000001ull, k2p32 - 1,
                               k2p32 + 1, k2p63, kUmax, 0xFFFFFFFFull, 1000000000000ull};
    for (const uint64_t a : vecs_a)
    {
        for (const uint64_t b : vecs_b)
        {
            if (b == 0)
            {
                continue;
            }
            // Single-word divisor with n1 >= d0 (two-divl path), single-word
            // divisor with n1 < d0, and the general (two-word divisor) path
            // are all covered by this matrix.
            CHECK(__udivdi3(a, b) == a / b);
            CHECK(__umoddi3(a, b) == a % b);

            uint64_t r = 0;
            CHECK(__udivmoddi4(a, b, &r) == a / b);
            CHECK(r == a % b);
            r = 0xCCCCCCCCCCCCCCCCull; // must be overwritten
            CHECK(__udivmoddi4(a, b, &r) == a / b);
            CHECK(r == a % b);
        }
    }

    // Divide-by-zero: quotient 0, remainder dividend (defined behavior).
    CHECK(__udivdi3(0, 0) == 0);
    CHECK(__udivdi3(kUmax, 0) == 0);
    CHECK(__udivdi3(k2p63, 0) == 0);
    CHECK(__umoddi3(0, 0) == 0);
    CHECK(__umoddi3(kUmax, 0) == kUmax);
    CHECK(__umoddi3(42, 0) == 42);
    uint64_t r = 0xDEADBEEFCAFEBABEull;
    CHECK(__udivmoddi4(kUmax, 0, &r) == 0);
    CHECK(r == kUmax);
    // rp == NULL must not crash.
    CHECK(__udivmoddi4(kUmax, 7, nullptr) == kUmax / 7);
    CHECK(__udivmoddi4(kUmax, 0, nullptr) == 0);
}

void test_div_mod_signed()
{
    const int64_t vecs_a[] = {0, 1, -1, 7, -7, 1000, -1000, kImax, kImin, kImin + 1,
                              -(int64_t)(k2p32 - 1), 1000000000000ll, -1000000000000ll,
                              1234567890123456789ll};
    const int64_t vecs_b[] = {1, 2, -1, 3, -3, 7, -7, 1000, -1000, kImax, k2p32 - 1, -5};
    for (const int64_t a : vecs_a)
    {
        for (const int64_t b : vecs_b)
        {
            if (b == 0 || (a == kImin && b == -1))
            {
                continue; // native UB; covered by the explicit cases below
            }
            CHECK(__divdi3(a, b) == a / b);
            CHECK(__moddi3(a, b) == a % b);
        }
    }

    // Divide-by-zero: quotient 0, remainder dividend.
    CHECK(__divdi3(0, 0) == 0);
    CHECK(__divdi3(kImax, 0) == 0);
    CHECK(__divdi3(kImin, 0) == 0);
    CHECK(__moddi3(0, 0) == 0);
    CHECK(__moddi3(42, 0) == 42);
    CHECK(__moddi3(-42, 0) == -42);
    CHECK(__moddi3(kImin, 0) == kImin);

    // INT64_MIN / -1: |a|=2^63 / 1 = 2^63, negated -> 0x8000000000000000
    // (sign-magnitude wrap, matching the reference semantics).
    CHECK(__divdi3(kImin, -1) == kImin);
    CHECK(__moddi3(kImin, -1) == 0);
}

void test_neg()
{
    CHECK(__negdi2(0) == 0);
    CHECK(__negdi2(1) == -1);
    CHECK(__negdi2(-1) == 1);
    CHECK(__negdi2(kImax) == kImin + 1);
    CHECK(__negdi2(kImin + 1) == kImax);
    CHECK(__negdi2(kImin) == kImin); // 0 - INT64_MIN wraps to INT64_MIN
    CHECK(__negdi2(1234567890123456789ll) == -1234567890123456789ll);
}

void test_shifts()
{
    const uint64_t x = 0xDEADBEEF12345678ull;
    const int64_t sx = (int64_t)x;
    for (int c = 0; c <= 70; ++c)
    {
        uint64_t exp_l = (c >= 64) ? 0ull : (x << c);
        CHECK(__ashldi3(x, c) == exp_l);
        uint64_t exp_r = (c >= 64) ? 0ull : (x >> c);
        CHECK(__lshrdi3(x, c) == exp_r);
        int64_t exp_ar = (c >= 64) ? (sx < 0 ? -1 : 0) : (sx >> c);
        CHECK(__ashrdi3(sx, c) == exp_ar);
    }
    // Boundary patterns: 32/33/63/64 for a value with a nonzero high word.
    const uint64_t hi = 0x8000000000000000ull;
    CHECK(__ashldi3(hi, 32) == 0);
    CHECK(__lshrdi3(hi, 63) == 1);
    CHECK(__ashrdi3((int64_t)hi, 63) == -1);
    CHECK(__ashrdi3((int64_t)hi, 64) == -1);
    CHECK(__ashrdi3((int64_t)hi, 65) == -1);
    CHECK(__ashrdi3(1, 64) == 0);
}

void test_cmp()
{
    const int64_t vecs[] = {kImin, kImin + 1, -1000, -1, 0, 1, 1000, kImax - 1, kImax};
    for (const int64_t a : vecs)
    {
        for (const int64_t b : vecs)
        {
            const int64_t exp = (a > b) ? 1 : (a < b ? -1 : 0);
            CHECK(__cmpdi2(a, b) == exp);
        }
    }
}

} // namespace

int main()
{
    test_mul();
    test_udiv_umod();
    test_div_mod_signed();
    test_neg();
    test_shifts();
    test_cmp();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "libgcc32_helpers_tests: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("libgcc32_helpers_tests: OK (mul/udiv/umod/div/mod/neg/shift/cmp vs native)\n");
    return 0;
}
