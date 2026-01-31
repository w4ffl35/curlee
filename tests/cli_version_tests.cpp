#include <cctype>
#include <cstdlib>
#include <curlee/cli/cli.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static bool is_hex_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_semverish(const std::string& s)
{
    // Keep this intentionally permissive: digits and dots, must contain at least one digit.
    bool saw_digit = false;
    for (char c : s)
    {
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            saw_digit = true;
            continue;
        }
        if (c == '.')
        {
            continue;
        }
        return false;
    }
    return saw_digit;
}

int main()
{
    std::ostringstream captured;
    auto* old_buf = std::cout.rdbuf(captured.rdbuf());

    std::vector<std::string> argv_storage = {"curlee", "--version"};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size());
    for (auto& s : argv_storage)
    {
        argv.push_back(s.data());
    }

    const int rc = curlee::cli::run(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old_buf);

    if (rc != 0)
    {
        fail("expected --version to exit 0");
    }

    const std::string out = captured.str();
    if (out.empty())
    {
        fail("expected output");
    }

    // Must be a single line.
    std::size_t nl_count = 0;
    for (char c : out)
    {
        if (c == '\n')
        {
            ++nl_count;
        }
    }
    if (nl_count != 1 || out.back() != '\n')
    {
        fail("expected exactly one newline-terminated line");
    }

    const std::string prefix = "curlee ";
    if (out.rfind(prefix, 0) != 0)
    {
        fail("expected output to start with 'curlee '");
    }

    // Expected stable tokens: curlee <semver> sha=<...> build=<...>
    // Keep assertions boring: format-only, not value-specific.
    const std::string line = out.substr(0, out.size() - 1);

    const auto sha_pos = line.find(" sha=");
    const auto build_pos = line.find(" build=");
    if (sha_pos == std::string::npos || build_pos == std::string::npos || !(sha_pos < build_pos))
    {
        fail("expected ' sha=' and ' build=' tokens in order");
    }

    const std::string semver = line.substr(prefix.size(), sha_pos - prefix.size());
    if (!is_semverish(semver))
    {
        fail("expected semver-like version token");
    }

    const std::string sha_value = line.substr(sha_pos + std::string(" sha=").size(),
                                              build_pos - (sha_pos + std::string(" sha=").size()));
    if (sha_value != "unknown")
    {
        if (sha_value.size() != 8)
        {
            fail("expected 8-char sha or 'unknown'");
        }
        for (char c : sha_value)
        {
            if (!is_hex_char(c))
            {
                fail("sha contained non-hex characters");
            }
        }
    }

    const std::string build_value = line.substr(build_pos + std::string(" build=").size());
    if (build_value.empty())
    {
        fail("expected non-empty build type");
    }

    std::cout << "OK\n";
    return 0;
}
