#include <cstdlib>
#include <curlee/cli/cli.h>
#include <curlee/compiler/emitter.h>
#include <curlee/diag/render.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <curlee/types/type_check.h>
#include <curlee/verification/checker.h>
#include <curlee/vm/value.h>
#include <curlee/vm/vm.h>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    out << "  curlee <file.curlee>\n";
    out << "  curlee lex <file.curlee>\n";
    out << "  curlee parse <file.curlee>\n";
    out << "  curlee check <file.curlee>\n";
    out << "  curlee run <file.curlee>\n";
    out << "  curlee fmt [--check] <file>\n";
}

bool ends_with(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
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

    auto run_checks = [&](parser::Program& program) -> bool
    {
        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), file);
            return false;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        program = std::move(std::get<parser::Program>(parsed));
        const auto resolved = resolver::resolve(program, file);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(resolved))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(resolved);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        const auto typed = types::type_check(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(typed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(typed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        const auto& type_info = std::get<types::TypeInfo>(typed);
        const auto verified = verification::verify(program, type_info);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(verified))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(verified);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        return true;
    };

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
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        return kExitOk;
    }

    if (cmd == "run")
    {
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        const auto emitted = compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(emitted))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(emitted);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return kExitError;
        }

        const auto& chunk = std::get<vm::Chunk>(emitted);
        vm::VM machine;
        const auto result = machine.run(chunk, 10000);
        if (!result.ok)
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = result.error;
            d.span = result.error_span;
            std::cerr << diag::render(d, file);
            return kExitError;
        }

        std::cout << "curlee run: result " << vm::to_string(result.value) << "\n";
        return kExitOk;
    }

    std::cerr << "error: unknown command: " << cmd << "\n";
    return kExitUsage;
}

int cmd_fmt(const std::string& path, bool check)
{
    std::string escaped = path;
    std::string::size_type pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos)
    {
        escaped.insert(pos, "\\");
        pos += 2;
    }

    std::string cmd = "clang-format -style=file ";
    if (check)
    {
        cmd += "--dry-run --Werror ";
    }
    else
    {
        cmd += "-i ";
    }
    cmd += "\"" + escaped + "\"";

    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::cerr << "error: clang-format failed\n";
        return kExitError;
    }
    return kExitOk;
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

    // Python-style shorthand: `curlee path/to/file.curlee` is the same as `curlee run path/to/file.curlee`.
    if (argc == 2 && !first.starts_with('-') && ends_with(first, ".curlee"))
    {
        return cmd_read_only("run", std::string(first));
    }

    const std::string_view cmd = argv[1];
    std::vector<std::string_view> args;
    for (int i = 2; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }

    if (cmd == "fmt")
    {
        bool check = false;
        if (args.size() == 2 && args[0] == "--check")
        {
            check = true;
            args.erase(args.begin());
        }

        if (args.size() != 1)
        {
            std::cerr << "error: expected curlee fmt [--check] <file>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        return cmd_fmt(std::string(args[0]), check);
    }

    if (argc != 3)
    {
        std::cerr << "error: expected <command> <file.curlee>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string path = argv[2];
    return cmd_read_only(cmd, path);
}

} // namespace curlee::cli
