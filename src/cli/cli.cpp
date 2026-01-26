#include <cstdlib>
#include <curlee/cli/cli.h>
#include <curlee/diag/render.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <iostream>
#include <optional>
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
    out << "  curlee lex <file.curlee>\n";
    out << "  curlee parse <file.curlee>\n";
    out << "  curlee check <file.curlee>\n";
    out << "  curlee run <file.curlee>\n";
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
        const source::SourceFile pseudo_file{.path = path, .contents = ""};
        const diag::Diagnostic diag{
            .severity = diag::Severity::Error,
            .message = err->message,
            .span = std::nullopt,
            .notes = {},
        };

        std::cerr << diag::render(diag, pseudo_file);
        return kExitError;
    }

    const auto& file = std::get<source::SourceFile>(loaded);

    if (cmd == "lex")
    {
        const auto res = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(res))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(res), file);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(res);
        std::cout << "curlee lex: " << toks.size() << " tokens\n";
        return kExitOk;
    }

    if (cmd == "parse")
    {
        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), file);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return kExitError;
        }

        const auto& program = std::get<parser::Program>(parsed);
        std::cout << parser::dump(program) << "\n";
        return kExitOk;
    }

    if (cmd == "check")
    {
        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), file);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return kExitError;
        }

        const auto& program = std::get<parser::Program>(parsed);
        const auto resolved = resolver::resolve(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(resolved))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(resolved);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return kExitError;
        }

        // TODO: binder/types/contracts/verifier pipeline.
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = "check not implemented yet";
        d.span = std::nullopt;
        std::cerr << diag::render(d, file);
        return kExitError;
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
        std::cerr << "error: expected <command> <file.curlee>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string_view cmd = argv[1];
    const std::string path = argv[2];

    return cmd_read_only(cmd, path);
}

} // namespace curlee::cli
