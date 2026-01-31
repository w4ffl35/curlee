#include <cstdlib>
#include <curlee/source/line_map.h>
#include <iostream>
#include <string>

static void expect_eq(std::size_t got, std::size_t expected, const char* what)
{
    if (got != expected)
    {
        std::cerr << "FAIL: " << what << ": got=" << got << " expected=" << expected << "\n";
        std::exit(1);
    }
}

int main()
{
    const std::string text = "a\n"  // line 1
                             "bc\n" // line 2
                             "def"; // line 3 (no trailing newline)

    curlee::source::LineMap map(text);

    {
        const auto lc = map.offset_to_line_col(0);
        expect_eq(lc.line, 1, "offset 0 line");
        expect_eq(lc.col, 1, "offset 0 col");
    }

    {
        const auto lc = map.offset_to_line_col(1); // '\n'
        expect_eq(lc.line, 1, "offset 1 line");
        expect_eq(lc.col, 2, "offset 1 col");
    }

    {
        const auto lc = map.offset_to_line_col(2); // 'b'
        expect_eq(lc.line, 2, "offset 2 line");
        expect_eq(lc.col, 1, "offset 2 col");
    }

    {
        const auto lc = map.offset_to_line_col(text.size()); // end
        expect_eq(lc.line, 3, "end line");
        expect_eq(lc.col, 4, "end col"); // "def" length 3 => col 4 at end
    }

    {
        const auto lc = map.offset_to_line_col(text.size() + 123); // clamp past end
        expect_eq(lc.line, 3, "clamped end line");
        expect_eq(lc.col, 4, "clamped end col");
    }

    {
        expect_eq(map.line_start_offset(0), 0, "line 0 start offset");
    }

    {
        expect_eq(map.line_start_offset(1), 0, "line 1 start offset");
        expect_eq(map.line_start_offset(2), 2, "line 2 start offset");
        expect_eq(map.line_start_offset(3), 5, "line 3 start offset");
    }

    {
        expect_eq(map.line_start_offset(999), text.size(), "too-large line start offset");
    }

    {
        expect_eq(map.line_count(), 3, "line_count");
    }

    std::cout << "OK\n";
    return 0;
}
