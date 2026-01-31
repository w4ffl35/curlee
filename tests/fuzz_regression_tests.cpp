#include <algorithm>
#include <cstdint>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

namespace
{

std::vector<std::filesystem::path> collect_files(const std::filesystem::path& dir)
{
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(dir))
    {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
    {
        if (entry.is_regular_file())
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::uint8_t> read_all_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return {};
    }

    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

void run_lexer_fixture(const std::filesystem::path& path)
{
    const auto bytes = read_all_bytes(path);
    const std::string input(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    (void)curlee::lexer::lex(input);
}

void run_parser_fixture(const std::filesystem::path& path)
{
    const auto bytes = read_all_bytes(path);
    const std::string input(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    const auto lexed = curlee::lexer::lex(input);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        return;
    }

    const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
    (void)curlee::parser::parse(tokens);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: " << argv[0] << " <tests/fuzz_regressions/>\n";
        return 2;
    }

    const std::filesystem::path root(argv[1]);

    const auto lexer_files = collect_files(root / "lexer");
    const auto parser_files = collect_files(root / "parser");

    for (const auto& path : lexer_files)
    {
        run_lexer_fixture(path);
    }

    for (const auto& path : parser_files)
    {
        run_parser_fixture(path);
    }

    return 0;
}
