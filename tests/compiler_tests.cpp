#include <cstdlib>
#include <curlee/compiler/emitter.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/vm/vm.h>
#include <iostream>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

int main()
{
    const std::string source = "fn main() -> Int { let x: Int = 1; return x + 2; }";

    const auto lexed = curlee::lexer::lex(source);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        fail("expected lexing to succeed in compiler test");
    }

    const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
    const auto parsed = curlee::parser::parse(tokens);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        fail("expected parsing to succeed in compiler test");
    }

    const auto& program = std::get<curlee::parser::Program>(parsed);
    const auto emitted = curlee::compiler::emit_bytecode(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
    {
        fail("expected bytecode emission to succeed");
    }

    const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
    curlee::vm::VM vm;
    const auto res = vm.run(chunk);
    if (!res.ok)
    {
        fail("expected VM to run compiled chunk");
    }
    if (!(res.value == curlee::vm::Value::int_v(3)))
    {
        fail("expected compiled result to equal 3");
    }

    std::cout << "OK\n";
    return 0;
}
