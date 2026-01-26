#include <curlee/compiler/emitter.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <curlee/types/type_check.h>
#include <curlee/verification/checker.h>
#include <curlee/vm/vm.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::ifstream in(path);
    if (!in)
    {
        fail("unable to read file: " + path.string());
    }
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return contents;
}

static void check_program(const fs::path& path, bool run_vm)
{
    const std::string contents = read_file(path);
    const curlee::source::SourceFile file{.path = path.string(), .contents = contents};

    const auto lexed = curlee::lexer::lex(contents);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        fail("lex failed for " + path.string());
    }

    const auto& toks = std::get<std::vector<curlee::lexer::Token>>(lexed);
    auto parsed = curlee::parser::parse(toks);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        fail("parse failed for " + path.string());
    }

    auto program = std::move(std::get<curlee::parser::Program>(parsed));
    const auto resolved = curlee::resolver::resolve(program, file);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(resolved))
    {
        fail("resolve failed for " + path.string());
    }

    const auto typed = curlee::types::type_check(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        fail("type check failed for " + path.string());
    }

    const auto& type_info = std::get<curlee::types::TypeInfo>(typed);
    const auto verified = curlee::verification::verify(program, type_info);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(verified))
    {
        fail("verification failed for " + path.string());
    }

    if (run_vm)
    {
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("bytecode emit failed for " + path.string());
        }
        const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
        curlee::vm::VM vm;
        const auto result = vm.run(chunk, 10000);
        if (!result.ok)
        {
            fail("vm run failed for " + path.string());
        }
    }
}

static void run_dir(const fs::path& dir, bool run_vm)
{
    if (!fs::exists(dir))
    {
        fail("missing directory: " + dir.string());
    }

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".curlee")
        {
            continue;
        }
        check_program(path, run_vm);
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fail("usage: curlee_e2e_tests <run_dir> <correct_samples_dir>");
    }

    run_dir(argv[1], true);
    run_dir(argv[2], false);

    std::cout << "OK\n";
    return 0;
}
