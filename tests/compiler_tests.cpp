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
    }

    {
        const std::string source = "fn main() -> Bool { return true; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed in bool compiler test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed in bool compiler test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected bytecode emission to succeed for bool literal");
        }

        const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
        curlee::vm::VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok)
        {
            fail("expected VM to run compiled bool chunk");
        }
        if (!(res.value == curlee::vm::Value::bool_v(true)))
        {
            fail("expected compiled result to equal true");
        }
    }

    {
        const std::string source =
            "fn main() -> Int { if (true) { return 1; } else { return 2; } }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed in if/else compiler test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed in if/else compiler test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected bytecode emission to succeed for if/else");
        }

        const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
        curlee::vm::VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(1)))
        {
            fail("expected if/else to return 1");
        }
    }

    {
        const std::string source =
            "fn main() -> Int { while (true) { return 42; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed in while compiler test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed in while compiler test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected bytecode emission to succeed for while");
        }

        const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
        curlee::vm::VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected while to return 42");
        }
    }

    {
        const std::string source =
            "fn foo() -> Int { return 7; } fn main() -> Int { return foo() + 1; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed in call compiler test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed in call compiler test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected bytecode emission to succeed for call");
        }

        const auto& chunk = std::get<curlee::vm::Chunk>(emitted);
        curlee::vm::VM vm;
        const auto res = vm.run(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(8)))
        {
            fail("expected foo() + 1 to equal 8");
        }
    }

    std::cout << "OK\n";
    return 0;
}
