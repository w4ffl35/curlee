#include <cstdlib>
#include <curlee/cli/cli.h>
#include <curlee/source/source_file.h>
#include <iostream>
#include <string>
#include <string_view>

namespace curlee::cli
{

namespace
{

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

void print_usage(std::ostream& out)
{
    out << "curlee: verification-first language (early scaffold)\n\n";
    out << "usage:\n";
    out << "  curlee --help\n";
    out << "  curlee lex <file.cur>\n";
    out << "  curlee parse <file.cur>\n";
    out << "  curlee check <file.cur>\n";
    out << "  curlee run <file.cur>\n";
}

bool is_help_flag(std::string_view arg)
{
    return arg == "--help" || arg == "-h" || arg == "help";
}

int cmd_read_only(std::string_view cmd, const std::string& path)
{
    auto loaded = source::load_source_file(path);
    if (auto* err = std::get_if<source::LoadError>(&loaded))
    {
        std::cerr << "error: " << err->message << "\n";
        return kExitError;
    }

    const auto& file = std::get<source::SourceFile>(loaded);

    if (cmd == "lex")
    {
        std::cout << "curlee lex: read " << file.contents.size() << " bytes from " << file.path
                  << "\n";
        return kExitOk;
    }

    if (cmd == "parse")
    {
        std::cout << "curlee parse: read " << file.contents.size() << " bytes from " << file.path
                  << " (parser not implemented yet)\n";
        return kExitOk;
    }

    if (cmd == "check")
    {
        std::cout << "curlee check: read " << file.contents.size() << " bytes from " << file.path
                  << " (verifier not implemented yet)\n";
        return kExitOk;
    }

    if (cmd == "run")
    {
        std::cout << "curlee run: read " << file.contents.size() << " bytes from " << file.path
                  << " (VM not implemented yet)\n";
        return kExitOk;
    }

    std::cerr << "error: unknown command: " << cmd << "\n";
    return kExitUsage;
}

} // namespace

int run(int argc, char** argv)
{
    if (argc <= 1)
    {
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string_view first = argv[1];
    if (is_help_flag(first))
    {
        print_usage(std::cout);
        return kExitOk;
    }

    if (argc != 3)
    {
        std::cerr << "error: expected <command> <file.cur>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string_view cmd = argv[1];
    const std::string path = argv[2];

    return cmd_read_only(cmd, path);
}

} // namespace curlee::cli
