#include <cstdlib>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    namespace fs = std::filesystem;
    using namespace curlee::source;

    const fs::path tmp = fs::path{"source_file_tests_tmp.curlee"};
    (void)fs::remove(tmp);

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            fail("failed to create temp file");
        }
        out << "hello\nworld";
    }

    // Success path.
    {
        const auto res = load_source_file(tmp.string());
        if (!std::holds_alternative<SourceFile>(res))
        {
            fail("expected SourceFile for readable temp file");
        }
        const auto& sf = std::get<SourceFile>(res);
        if (sf.path != tmp.string())
        {
            fail("unexpected path");
        }
        if (sf.contents != "hello\nworld")
        {
            fail("unexpected contents");
        }
    }

    // Open-failure path.
    {
        const auto res = load_source_file("this_file_should_not_exist_hopefully.curlee");
        if (!std::holds_alternative<LoadError>(res))
        {
            fail("expected LoadError for missing file");
        }
        const auto& err = std::get<LoadError>(res);
        if (err.message != "failed to open file")
        {
            fail("unexpected error message: " + err.message);
        }
    }

    // Read-failure path.
    {
        std::istringstream in("hello");
        in.setstate(std::ios::badbit);

        const auto res = load_source_stream(in, "synthetic.curlee");
        if (!std::holds_alternative<LoadError>(res))
        {
            fail("expected LoadError for bad stream");
        }
        const auto& err = std::get<LoadError>(res);
        if (err.message != "failed while reading file")
        {
            fail("unexpected error message: " + err.message);
        }
    }

    // EOF-only path: stream is not good(), but eof() is true, so this is not a read error.
    {
        std::istringstream in("");
        // Ensure eofbit is set.
        (void)in.get();

        const auto res = load_source_stream(in, "empty.curlee");
        if (!std::holds_alternative<SourceFile>(res))
        {
            fail("expected SourceFile for EOF-only stream");
        }
        const auto& sf = std::get<SourceFile>(res);
        if (sf.path != "empty.curlee")
        {
            fail("unexpected path");
        }
        if (sf.contents != "")
        {
            fail("expected empty contents for EOF-only stream");
        }
    }

    (void)fs::remove(tmp);
    std::cout << "OK\n";
    return 0;
}
