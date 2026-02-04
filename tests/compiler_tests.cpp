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

    // Builtin print should enforce argument count.
    {
        const std::string source = "fn main() -> Int { print(1, 2); return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for print argcount case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for print argcount case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for print argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("print expects exactly 1 argument") == std::string::npos)
        {
            fail("expected print argcount diagnostic");
        }
    }

    // python_ffi.call(...) should lower to a PythonCall opcode.
    {
        const std::string source = "fn main() -> Int { python_ffi.call(); return 0; }";
        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::PythonCall))
        {
            fail("expected python_ffi.call() to emit PythonCall opcode");
        }
    }

    {
        // python_ffi.<member>(...) should NOT be treated as PythonCall.
        const std::string source = "fn main() -> Int { return python_ffi.nope(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for python_ffi non-call member case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for python_ffi non-call member case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for python_ffi non-call member case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown module qualifier") == std::string::npos)
        {
            fail("expected unknown module qualifier diagnostic for python_ffi.nope()");
        }
    }

    // main cannot take parameters in runnable code.
    {
        const std::string source = "fn main(x: Int) -> Int { return x; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for main params case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for main params case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for main params case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("main cannot take parameters") == std::string::npos)
        {
            fail("expected main params diagnostic");
        }
    }

    // Only Int and Bool parameter types are supported in runnable code.
    {
        const std::string source = "fn foo(x: String) -> Int { return 0; }"
                                   "fn main() -> Int { return 0; }";

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
            fail("expected emission to fail for unsupported param type case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("parameter type not supported in runnable code") ==
                std::string::npos)
        {
            fail("expected unsupported parameter type diagnostic");
        }
    }

    {
        // String literal escape decoding should support common escapes.
        const std::string source = "fn main() -> String { return \"\\n\\t\\r\\\\\\\"\"; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::string_v("\n\t\r\\\"")))
        {
            fail("expected string escapes to decode correctly");
        }
    }

    {
        // Unsupported escapes in string literals should be rejected by the emitter.
        const std::string source = "fn main() -> String { return \"\\q\"; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unsupported escape case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unsupported escape case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unsupported escape case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unsupported escape \\q'") == std::string::npos)
        {
            fail("expected unsupported escape diagnostic");
        }
    }

    {
        // Force string literal decoding errors via manual AST construction.
        using curlee::parser::Expr;
        using curlee::parser::Function;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::StringExpr;
        using curlee::parser::TypeName;

        auto run_with_lexeme = [](std::string_view lexeme)
        {
            Expr lit;
            lit.node = StringExpr{.lexeme = lexeme};

            Stmt ret_stmt;
            ret_stmt.node = ReturnStmt{.value = std::move(lit)};

            Function main_fn;
            main_fn.name = "main";
            main_fn.return_type = TypeName{.span = {}, .name = "String"};
            main_fn.body.stmts.push_back(std::move(ret_stmt));

            Program program;
            program.functions.push_back(std::move(main_fn));
            return curlee::compiler::emit_bytecode(program);
        };

        {
            const auto emitted = run_with_lexeme("\"no_end");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for malformed string literal");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("malformed string literal") == std::string::npos)
            {
                fail("expected malformed string literal diagnostic");
            }
        }
    }

    {
        // Unknown local names should be rejected.
        const std::string source = "fn main() -> Int { return x; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unknown name case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unknown name case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unknown name case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic");
        }
    }

    {
        // Calls should enforce argument counts.
        const std::string source =
            "fn id(x: Int) -> Int { return x; } fn main() -> Int { return id(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for argcount mismatch call case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for argcount mismatch call case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for argcount mismatch call case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("expects 1 argument") == std::string::npos)
        {
            fail("expected call argcount mismatch diagnostic");
        }
    }

    {
        // Expression statements should propagate expression-emission errors.
        const std::string source = "fn main() -> Int { x; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for expr-stmt unknown name case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for expr-stmt unknown name case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for expr-stmt unknown name case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for expr statement");
        }
    }

    {
        // Nested blocks should stop emission when a nested stmt fails.
        const std::string source = "fn main() -> Int { { x; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for block nested error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for block nested error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for block nested error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for nested block");
        }
    }

    {
        // Unsafe blocks should stop emission when a nested stmt fails.
        const std::string source = "fn main() -> Int { unsafe { x; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unsafe nested error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unsafe nested error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unsafe nested error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for unsafe block");
        }
    }

    {
        // If condition emission failures should stop statement emission.
        const std::string source = "fn main() -> Int { if (x) { return 0; } return 1; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for if-cond error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for if-cond error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for if-cond error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for if condition");
        }
    }

    {
        // If then-block emission failures should stop statement emission.
        const std::string source = "fn main() -> Int { if (true) { x; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for if-then error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for if-then error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for if-then error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for if then-block");
        }
    }

    {
        // If else-block emission failures should stop statement emission.
        const std::string source =
            "fn main() -> Int { if (false) { return 0; } else { x; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for if-else error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for if-else error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for if-else error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for if else-block");
        }
    }

    {
        // While body emission failures should stop statement emission.
        const std::string source = "fn main() -> Int { while (true) { x; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for while-body error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for while-body error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for while-body error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for while body");
        }
    }

    {
        // While condition emission failures should stop statement emission.
        const std::string source = "fn main() -> Int { while (x) { return 0; } return 1; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for while-cond error case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for while-cond error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for while-cond error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for while condition");
        }
    }

    {
        // Builtin print should stop if argument expression emission fails.
        const std::string source = "fn main() -> Int { print(x); return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for print-arg emission failure case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for print-arg emission failure case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for print-arg emission failure case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for print arg");
        }
    }

    {
        // Calls should stop if argument expression emission fails.
        const std::string source =
            "fn id(x: Int) -> Int { return x; } fn main() -> Int { return id(y); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for call-arg emission failure case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for call-arg emission failure case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for call-arg emission failure case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'y'") == std::string::npos)
        {
            fail("expected unknown name diagnostic for call arg");
        }
    }

    {
        // An if without else should compile and run.
        const std::string source = "fn main() -> Int { if (true) { return 1; } return 2; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(1)))
        {
            fail("expected if-without-else to return 1");
        }
    }

    {
        // Unsafe and nested block statements should be emitted.
        const std::string source = "fn main() -> Int { unsafe { { let x: Int = 1; } } return 0; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected unsafe and nested blocks to run");
        }
    }

    {
        // Group-expression callees should be rejected (callee must be a name or module-qualified).
        const std::string source =
            "fn foo() -> Int { return 1; } fn main() -> Int { return (foo)(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for group-callee call case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for group-callee call case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for group-callee call case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("only name calls and module-qualified calls") ==
                                 std::string::npos)
        {
            fail("expected runnable-call restriction diagnostic for group-callee call");
        }
    }

    {
        // Unsupported expression forms should be rejected by the emitter.
        const std::string source_member = "fn main() -> Int { let a: Int = 0; return a.b; }";
        const auto lexed = curlee::lexer::lex(source_member);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for member access expr case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for member access expr case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for member access expr case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("member access not supported") == std::string::npos)
        {
            fail("expected member access diagnostic");
        }
    }

    {
        const std::string source_scoped = "fn main() -> Int { return a::b; }";
        const auto lexed = curlee::lexer::lex(source_scoped);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for scoped name expr case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for scoped name expr case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for scoped name expr case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("scoped names (::) not supported") == std::string::npos)
        {
            fail("expected scoped name diagnostic");
        }
    }

    {
        // Force unsupported operator/expr diagnostics via manual AST construction.
        using curlee::lexer::TokenKind;
        using curlee::parser::BinaryExpr;
        using curlee::parser::BoolExpr;
        using curlee::parser::Expr;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::NameExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::StringExpr;
        using curlee::parser::StructLiteralExpr;
        using curlee::parser::TypeName;
        using curlee::parser::UnaryExpr;

        auto run_with_expr = [](Expr expr, std::string_view return_type = "Int")
        {
            Stmt ret_stmt;
            ret_stmt.node = ReturnStmt{.value = std::move(expr)};

            Function main_fn;
            main_fn.name = "main";
            main_fn.return_type = TypeName{.span = {}, .name = return_type};
            main_fn.body.stmts.push_back(std::move(ret_stmt));

            Program program;
            program.functions.push_back(std::move(main_fn));
            return curlee::compiler::emit_bytecode(program);
        };

        {
            Expr rhs;
            rhs.node = IntExpr{.lexeme = "1"};

            Expr root;
            root.node =
                UnaryExpr{.op = TokenKind::Plus, .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for unsupported unary op case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("unsupported unary operator") == std::string::npos)
            {
                fail("expected unsupported unary operator diagnostic");
            }
        }

        {
            Expr lhs;
            lhs.node = IntExpr{.lexeme = "1"};
            Expr rhs;
            rhs.node = IntExpr{.lexeme = "2"};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::Equal,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for unsupported binary op case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("unsupported binary operator") == std::string::npos)
            {
                fail("expected unsupported binary operator diagnostic");
            }
        }

        {
            Expr root;
            root.node = StructLiteralExpr{.type_name = "T", .fields = {}};
            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for struct literal expr case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("struct literals not supported") == std::string::npos)
            {
                fail("expected struct literal diagnostic");
            }
        }

        {
            // String literal decoding failures should be diagnosed.
            Expr root;
            root.node = StringExpr{.lexeme = "abc"};
            const auto emitted = run_with_expr(std::move(root), "String");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for malformed string literal case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("malformed string literal") == std::string::npos)
            {
                fail("expected malformed string literal diagnostic");
            }
        }

        {
            Expr root;
            root.node = StringExpr{.lexeme = ""};
            const auto emitted = run_with_expr(std::move(root), "String");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for empty string literal case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("malformed string literal") == std::string::npos)
            {
                fail("expected malformed string literal diagnostic for empty lexeme");
            }
        }

        {
            std::string lexeme = "\"\\\"";

            Expr root;
            root.node = StringExpr{.lexeme = lexeme};
            const auto emitted = run_with_expr(std::move(root), "String");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for unterminated escape case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unterminated escape") == std::string::npos)
            {
                fail("expected unterminated escape diagnostic");
            }
        }

        {
            // Unary emission should early-return if its operand emits diagnostics.
            Expr rhs;
            rhs.node = NameExpr{.name = "x"};

            Expr root;
            root.node =
                UnaryExpr{.op = TokenKind::Minus, .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for unary operand emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for unary operand emission failure");
            }
        }

        {
            // Short-circuit binary emission should early-return if rhs emits diagnostics.
            Expr lhs;
            lhs.node = BoolExpr{.value = true};
            Expr rhs;
            rhs.node = NameExpr{.name = "x"};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::AndAnd,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root), "Bool");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for && rhs emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for && rhs emission failure");
            }
        }

        {
            // Short-circuit binary emission should early-return if lhs emits diagnostics.
            Expr lhs;
            lhs.node = NameExpr{.name = "x"};
            Expr rhs;
            rhs.node = BoolExpr{.value = true};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::AndAnd,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root), "Bool");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for && lhs emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for && lhs emission failure");
            }
        }

        {
            // Short-circuit binary emission should early-return if lhs emits diagnostics (||).
            Expr lhs;
            lhs.node = NameExpr{.name = "x"};
            Expr rhs;
            rhs.node = BoolExpr{.value = false};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::OrOr,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root), "Bool");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for || lhs emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for || lhs emission failure");
            }
        }

        {
            // Short-circuit binary emission should early-return if rhs emits diagnostics (||).
            Expr lhs;
            lhs.node = BoolExpr{.value = true};
            Expr rhs;
            rhs.node = NameExpr{.name = "x"};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::OrOr,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root), "Bool");
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for || rhs emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for || rhs emission failure");
            }
        }

        {
            // Non-short-circuit binary emission should early-return if rhs emits diagnostics.
            Expr lhs;
            lhs.node = IntExpr{.lexeme = "1"};
            Expr rhs;
            rhs.node = NameExpr{.name = "x"};

            Expr root;
            root.node = BinaryExpr{.op = TokenKind::Plus,
                                   .lhs = std::make_unique<Expr>(std::move(lhs)),
                                   .rhs = std::make_unique<Expr>(std::move(rhs))};

            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for binary rhs emission failure case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'x'") == std::string::npos)
            {
                fail("expected unknown name diagnostic for binary rhs emission failure");
            }
        }

        {
            // A callee that is a member expr with null base should be rejected.
            using curlee::parser::CallExpr;
            using curlee::parser::MemberExpr;

            Expr callee;
            callee.node = MemberExpr{.base = nullptr, .member = "f"};

            CallExpr call;
            call.callee = std::make_unique<Expr>(std::move(callee));

            Expr root;
            root.node = std::move(call);

            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for member-callee null base case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("only name calls and module-qualified") == std::string::npos)
            {
                fail("expected runnable-call restriction diagnostic for member-callee null base");
            }
        }

        {
            // Cover every supported binary-op switch case (non-short-circuit), plus default.
            auto emit_binary_ok = [&](TokenKind op, std::string_view return_type)
            {
                Expr lhs;
                lhs.node = IntExpr{.lexeme = "1"};
                Expr rhs;
                rhs.node = IntExpr{.lexeme = "2"};

                Expr root;
                root.node = BinaryExpr{.op = op,
                                       .lhs = std::make_unique<Expr>(std::move(lhs)),
                                       .rhs = std::make_unique<Expr>(std::move(rhs))};

                const auto emitted = run_with_expr(std::move(root), return_type);
                if (!std::holds_alternative<curlee::vm::Chunk>(emitted))
                {
                    fail("expected emission to succeed for supported binary op case");
                }
            };

            emit_binary_ok(TokenKind::Plus, "Int");
            emit_binary_ok(TokenKind::Minus, "Int");
            emit_binary_ok(TokenKind::Star, "Int");
            emit_binary_ok(TokenKind::Slash, "Int");

            emit_binary_ok(TokenKind::EqualEqual, "Bool");
            emit_binary_ok(TokenKind::BangEqual, "Bool");
            emit_binary_ok(TokenKind::Less, "Bool");
            emit_binary_ok(TokenKind::LessEqual, "Bool");
            emit_binary_ok(TokenKind::Greater, "Bool");
            emit_binary_ok(TokenKind::GreaterEqual, "Bool");

            {
                Expr lhs;
                lhs.node = IntExpr{.lexeme = "1"};
                Expr rhs;
                rhs.node = IntExpr{.lexeme = "2"};

                Expr root;
                root.node = BinaryExpr{.op = TokenKind::Comma,
                                       .lhs = std::make_unique<Expr>(std::move(lhs)),
                                       .rhs = std::make_unique<Expr>(std::move(rhs))};

                const auto emitted = run_with_expr(std::move(root));
                if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
                {
                    fail("expected emission to fail for unsupported binary op default case");
                }
                const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
                if (diags.empty() ||
                    diags[0].message.find("unsupported binary operator") == std::string::npos)
                {
                    fail("expected unsupported binary operator diagnostic for default case");
                }
            }
        }
    }

    {
        // Return without a value should implicitly return Unit.
        const std::string source = "fn main() -> Int { return; }";
        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::unit_v()))
        {
            fail("expected bare return to return unit");
        }
    }

    std::cout << "OK\n";
    return 0;
}
