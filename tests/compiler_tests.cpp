#include <cstdlib>
#include <curlee/compiler/emitter.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/vm/vm.h>
#include <iostream>
#include <memory>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static curlee::vm::Chunk compile_to_chunk(const std::string& source)
{
    const auto lexed = curlee::lexer::lex(source);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        fail("expected lexing to succeed");
    }

    const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
    const auto parsed = curlee::parser::parse(tokens);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        fail("expected parsing to succeed");
    }

    const auto& program = std::get<curlee::parser::Program>(parsed);
    const auto emitted = curlee::compiler::emit_bytecode(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
    {
        fail("expected bytecode emission to succeed");
    }

    return std::get<curlee::vm::Chunk>(emitted);
}

static curlee::vm::VmResult run_chunk(const curlee::vm::Chunk& chunk)
{
    curlee::vm::VM vm;
    return vm.run(chunk);
}

static curlee::vm::VmResult run_chunk_with_caps(const curlee::vm::Chunk& chunk,
                                                const curlee::vm::VM::Capabilities& caps)
{
    curlee::vm::VM vm;
    return vm.run(chunk, caps);
}

static std::vector<curlee::vm::OpCode> decode_ops(const curlee::vm::Chunk& chunk)
{
    using curlee::vm::OpCode;
    std::vector<OpCode> ops;
    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        const auto op = static_cast<OpCode>(chunk.code[ip++]);
        ops.push_back(op);
        switch (op)
        {
        case OpCode::Constant:
        case OpCode::LoadLocal:
        case OpCode::StoreLocal:
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
        case OpCode::Call:
            ip += 2;
            break;
        case OpCode::Add:
        case OpCode::Sub:
        case OpCode::Mul:
        case OpCode::Div:
        case OpCode::Neg:
        case OpCode::Not:
        case OpCode::Equal:
        case OpCode::NotEqual:
        case OpCode::Less:
        case OpCode::LessEqual:
        case OpCode::Greater:
        case OpCode::GreaterEqual:
        case OpCode::Pop:
        case OpCode::Return:
        case OpCode::Ret:
        case OpCode::Print:
        case OpCode::PythonCall:
            break;
        }
    }
    return ops;
}

static bool contains_op(const std::vector<curlee::vm::OpCode>& ops, curlee::vm::OpCode needle)
{
    for (const auto op : ops)
    {
        if (op == needle)
        {
            return true;
        }
    }
    return false;
}

int main()
{
    {
        const std::string source = "fn main() -> Int { let x: Int = 1; return x + 2; }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
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

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
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

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(1)))
        {
            fail("expected if/else to return 1");
        }
    }

    {
        const std::string source = "fn main() -> Int { while (true) { return 42; } return 0; }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected while to return 42");
        }
    }

    {
        const std::string source =
            "fn foo() -> Int { return 7; } fn main() -> Int { return foo() + 1; }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(8)))
        {
            fail("expected foo() + 1 to equal 8");
        }
    }

    {
        const std::string source =
            "fn inc(x: Int) -> Int { return x + 1; } fn main() -> Int { return inc(41); }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected inc(41) to equal 42");
        }
    }

    {
        const std::string source = "fn add(x: Int, y: Int) -> Int { return x + y; } fn main() -> "
                                   "Int { return add(1, 2); }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(3)))
        {
            fail("expected add(1,2) to equal 3");
        }
    }

    {
        const std::string source =
            "fn negate(x: Bool) -> Bool { return !x; } fn main() -> Bool { return negate(false); }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::bool_v(true)))
        {
            fail("expected negate(false) to equal true");
        }
    }

    {
        const std::string source =
            "fn f(x: String) -> Int { return 1; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unsupported param type case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unsupported param type case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected bytecode emission to fail for unsupported parameter types");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("parameter type not supported in runnable code") ==
                std::string::npos)
        {
            fail("expected diagnostic for unsupported parameter type in runnable code");
        }
    }

    {
        const std::string source = "fn main() -> Int { return -1; }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::Neg))
        {
            fail("expected unary '-' to emit Neg opcode");
        }
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(-1)))
        {
            fail("expected unary '-' to evaluate to -1");
        }
    }

    {
        const std::string source = "fn main() -> Bool { return !false; }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::Not))
        {
            fail("expected '!' to emit Not opcode");
        }
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::bool_v(true)))
        {
            fail("expected '!false' to evaluate to true");
        }
    }

    {
        const std::string source = "fn main() -> Int { return 10 - 3 * 2; }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::Sub) ||
            !contains_op(ops, curlee::vm::OpCode::Mul))
        {
            fail("expected '-' and '*' to emit Sub and Mul opcodes");
        }
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(4)))
        {
            fail("expected 10 - 3 * 2 to equal 4");
        }
    }

    {
        const std::string source = "fn main() -> Bool { return (1 < 2) && (3 >= 3); }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::Less) ||
            !contains_op(ops, curlee::vm::OpCode::GreaterEqual))
        {
            fail("expected '<' and '>=' to emit comparison opcodes");
        }
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::bool_v(true)))
        {
            fail("expected comparison + && to evaluate to true");
        }
    }

    {
        // Short-circuit should avoid divide-by-zero on RHS.
        const std::string source =
            "fn main() -> Int { if (false && ((1 / 0) == 0)) { return 1; } return 2; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(2)))
        {
            fail("expected && short-circuit to avoid RHS evaluation");
        }
    }

    {
        // Short-circuit should avoid divide-by-zero on RHS.
        const std::string source =
            "fn main() -> Int { if (true || ((1 / 0) == 0)) { return 1; } return 2; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(1)))
        {
            fail("expected || short-circuit to avoid RHS evaluation");
        }
    }

    {
        const std::string source = "fn main() -> String { return \"a\" + \"b\"; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::string_v("ab")))
        {
            fail("expected string concatenation to evaluate to 'ab'");
        }
    }

    {
        const std::string source = "fn main() -> Int { print(\"hi\"); return 0; }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::Print))
        {
            fail("expected print(...) to emit Print opcode");
        }

        curlee::vm::VM::Capabilities caps;
        caps.insert("io:stdout");
        const auto res = run_chunk_with_caps(chunk, caps);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected print(...) program to run with io:stdout capability");
        }
    }

    // Emitter should reject duplicate function declarations.
    {
        const std::string source = "fn main() -> Int { return 0; } fn main() -> Int { return 1; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for duplicate function case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for duplicate function case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for duplicate function case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("duplicate function declaration") == std::string::npos)
        {
            fail("expected duplicate function diagnostic");
        }
    }

    // Emitter should reject declaring builtin print.
    {
        const std::string source = "fn print() -> Int { return 0; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin print case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin print case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin print case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function 'print'") == std::string::npos)
        {
            fail("expected builtin print diagnostic");
        }
    }

    // Emitter should report missing entry point.
    {
        const std::string source = "fn foo() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for missing main case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for missing main case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for missing main case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("no entry function 'main' found") == std::string::npos)
        {
            fail("expected missing main diagnostic");
        }
    }

    // Emitter should detect unresolved calls.
    {
        const std::string source = "fn main() -> Int { return foo(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unknown call case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unknown call case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unknown call case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown function 'foo'") == std::string::npos)
        {
            fail("expected unknown function diagnostic");
        }
    }

    // Module-qualified calls should accept imported aliases.
    {
        const std::string source = "import mymod.math as m;"
                                   "fn add1(x: Int) -> Int { return x + 1; }"
                                   "fn main() -> Int { return m.add1(41); }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected alias-qualified call to succeed");
        }
    }

    // Module-qualified calls should accept imported paths.
    {
        const std::string source = "import mymod.math;"
                                   "fn add1(x: Int) -> Int { return x + 1; }"
                                   "fn main() -> Int { return mymod.math.add1(1); }";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(2)))
        {
            fail("expected path-qualified call to succeed");
        }
    }

    // Module-qualified calls should reject unknown qualifiers.
    {
        const std::string source = "fn add1(x: Int) -> Int { return x + 1; }"
                                   "fn main() -> Int { return nope.add1(1); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unknown module qualifier case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unknown module qualifier case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unknown module qualifier case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("unknown module qualifier in runnable call") == std::string::npos)
        {
            fail("expected unknown module qualifier diagnostic");
        }
    }

    // collect_member_chain failure should trigger a runnable-call diagnostic.
    {
        const std::string source = "fn f() -> Int { return 0; }"
                                   "fn main() -> Int { return (1 + 2).f(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for non-name base member call case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for non-name base member call case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for non-name base member call case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("only name calls and module-qualified calls") ==
                                 std::string::npos)
        {
            fail("expected runnable-call restriction diagnostic");
        }
    }

    // Force the internal collect_member_chain null-base path via manual AST construction.
    {
        using curlee::parser::CallExpr;
        using curlee::parser::Expr;
        using curlee::parser::Function;
        using curlee::parser::MemberExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Expr inner_bad;
        inner_bad.node = MemberExpr{.base = nullptr, .member = "bad"};

        Expr outer;
        outer.node =
            MemberExpr{.base = std::make_unique<Expr>(std::move(inner_bad)), .member = "call"};

        CallExpr call;
        call.callee = std::make_unique<Expr>(std::move(outer));

        Expr call_expr;
        call_expr.node = std::move(call);

        Stmt ret_stmt;
        ret_stmt.node = ReturnStmt{.value = std::move(call_expr)};

        Function main_fn;
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        Program program;
        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for manual member chain null-base case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("only name calls and module-qualified calls") ==
                                 std::string::npos)
        {
            fail("expected runnable-call restriction diagnostic for null-base chain");
        }
    }

    std::cout << "OK\n";
    return 0;
}
