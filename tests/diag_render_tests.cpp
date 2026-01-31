#include <cstdlib>
#include <curlee/diag/diagnostic.h>
#include <curlee/diag/render.h>
#include <curlee/source/source_file.h>
#include <iostream>
#include <string>

static void expect_contains(const std::string& got, const std::string& needle, const char* what)
{
    if (got.find(needle) == std::string::npos)
    {
        std::cerr << "FAIL: " << what << ": expected output to contain: '" << needle << "'\n";
        std::cerr << "Got:\n" << got << "\n";
        std::exit(1);
    }
}

int main()
{
    const curlee::source::SourceFile file{
        .path = "test.curlee",
        .contents = "abc\n"  // line 1
                    "defg\n" // line 2
                    "hi\n",  // line 3
    };

    // No-span diagnostic: should render file path + severity + message and notes.
    {
        curlee::diag::Diagnostic diag;
        diag.severity = curlee::diag::Severity::Warning;
        diag.message = "something happened";
        diag.span = std::nullopt;
        diag.notes.push_back(curlee::diag::Related{.message = "note 1", .span = std::nullopt});

        const std::string out = curlee::diag::render(diag, file);
        expect_contains(out, "test.curlee: warning: something happened", "no-span header");
        expect_contains(out, "note: note 1", "no-span note");
    }

    // Span diagnostic: caret highlights within a line.
    {
        curlee::diag::Diagnostic diag;
        diag.severity = curlee::diag::Severity::Error;
        diag.message = "bad";
        diag.span = curlee::source::Span{.start = 5, .end = 7}; // "ef" in line 2
        diag.notes.push_back(curlee::diag::Related{.message = "helpful", .span = std::nullopt});

        const std::string out = curlee::diag::render(diag, file);
        expect_contains(out, "test.curlee:2:2: error: bad", "span header");
        expect_contains(out, "| defg", "source line");
        expect_contains(out, "^^", "caret len");
        expect_contains(out, "note: helpful", "span note");
    }

    // Multi-line span: only highlight first line.
    {
        curlee::diag::Diagnostic diag;
        diag.severity = curlee::diag::Severity::Error;
        diag.message = "cross";
        diag.span = curlee::source::Span{.start = 2, .end = 100};

        const std::string out = curlee::diag::render(diag, file);
        expect_contains(out, "test.curlee:1:3: error: cross", "multiline header");
        expect_contains(out, "| abc", "multiline line");
        // Should not print a giant run of carets; at least one caret.
        expect_contains(out, "^", "multiline caret");
    }

    // Invalid enum value hits the default severity_string() fallback.
    {
        curlee::diag::Diagnostic diag;
        diag.severity = static_cast<curlee::diag::Severity>(123);
        diag.message = "unknown severity";
        diag.span = std::nullopt;

        const std::string out = curlee::diag::render(diag, file);
        expect_contains(out, "test.curlee: error: unknown severity", "invalid severity fallback");
    }

    std::cout << "OK\n";
    return 0;
}
