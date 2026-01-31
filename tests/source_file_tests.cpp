#include <cstdlib>
#include <curlee/source/source_file.h>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    (void)fs::remove(tmp);
    std::cout << "OK\n";
    return 0;
}
