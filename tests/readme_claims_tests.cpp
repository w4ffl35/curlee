#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string slurp(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void expect_contains(const std::string& haystack, const std::string& needle,
                            const char* what)
{
    if (haystack.find(needle) == std::string::npos)
    {
        std::cerr << "FAIL: missing " << what << " (expected to contain: '" << needle << "')\n";
        std::exit(1);
    }
}

static void expect_not_contains(const std::string& haystack, const std::string& needle,
                                const char* what)
{
    if (haystack.find(needle) != std::string::npos)
    {
        std::cerr << "FAIL: contains stale " << what << " (unexpected: '" << needle << "')\n";
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: curlee_readme_claims_tests <path-to-README.md>\n";
        return 2;
    }

    const std::filesystem::path readme_path = argv[1];
    const std::string readme = slurp(readme_path);

    // Positive, stable anchors.
    expect_contains(readme, "No proof, no run.", "core motto");
    expect_contains(readme, "https://github.com/w4ffl35/curlee/wiki", "wiki link");
    expect_contains(readme, "Stability-and-Supported-Fragment", "supported fragment link");

    // Guard against a few known-stale claims.
    expect_not_contains(readme, "Calls: simple **no-arg** calls", "no-arg calls limitation");
    expect_not_contains(readme, "Out of scope (for now): strings", "out-of-scope list");
    expect_not_contains(readme, "modules/import execution", "imports not supported claim");

    std::cout << "OK\n";
    return 0;
}
