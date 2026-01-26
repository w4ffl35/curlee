#include <curlee/cli/cli.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        fail("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void write_file(const fs::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        fail("failed to write file: " + path.string());
    }
    out << contents;
}

int main()
{
    const fs::path temp_dir = fs::current_path() / "cli_fmt_tests";
    fs::create_directories(temp_dir);
    const fs::path file_path = temp_dir / "format_test.cpp";

    const std::string unformatted = "int   main(){return 0;}\n";
    const std::string formatted = "int main()\n{\n    return 0;\n}\n";

    write_file(file_path, unformatted);

    {
        std::vector<std::string> argv_storage = {"curlee", "fmt", file_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        if (rc != 0)
        {
            fail("expected fmt to succeed");
        }
    }

    const auto got = read_file(file_path);
    if (got != formatted)
    {
        fail("formatted output did not match expected style");
    }

    write_file(file_path, unformatted);

    {
        std::vector<std::string> argv_storage = {"curlee", "fmt", "--check", file_path.string()};
        std::vector<char*> argv;
        argv.reserve(argv_storage.size());
        for (auto& s : argv_storage)
        {
            argv.push_back(s.data());
        }

        const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
        if (rc == 0)
        {
            fail("expected fmt --check to fail on unformatted file");
        }
    }

    std::cout << "OK\n";
    return 0;
}
