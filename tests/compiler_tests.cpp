#include <cstdlib>
#include <curlee/compiler/emitter.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/vm/vm.h>
#include <iostream>
#include <limits>
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
        case OpCode::ReadLine:
        case OpCode::FsReadText:
        case OpCode::FsWriteText:
        case OpCode::TtyClear:
        case OpCode::TtyWriteAt:
        case OpCode::TtyFlush:
        case OpCode::RngNextInt:
        case OpCode::VecNew:
        case OpCode::VecLen:
        case OpCode::VecPush:
        case OpCode::VecGet:
        case OpCode::VecSet:
        case OpCode::VecNewBool:
        case OpCode::VecLenBool:
        case OpCode::VecPushBool:
        case OpCode::VecGetBool:
        case OpCode::VecSetBool:
        case OpCode::SetNewInt:
        case OpCode::SetHasInt:
        case OpCode::SetInsertInt:
        case OpCode::PythonCall:
            break;
        case OpCode::MakeEnum:
            ip += 5;
            break;
        case OpCode::EnumIs:
        case OpCode::EnumUnwrap:
        case OpCode::GetField:
            ip += 2;
            break;
        case OpCode::MakeStruct:
        {
            const std::uint16_t count_lo = chunk.code[ip + 2];
            const std::uint16_t count_hi = chunk.code[ip + 3];
            const std::uint16_t field_count = static_cast<std::uint16_t>(count_lo | (count_hi << 8));
            ip += static_cast<std::size_t>(4 + 2 * field_count);
            break;
        }
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
        const std::string source = R"(enum Maybe {
    Some(Int);
    None;
}
fn main() -> Int {
    let m: Maybe = Maybe::Some(7);
    match (m) {
        Maybe::Some(v) => { return v + 1; }
        Maybe::None => { return 0; }
    }
    return -1;
})";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok)
        {
            fail("expected VM to run compiled match chunk");
        }
        if (!(res.value == curlee::vm::Value::int_v(8)))
        {
            fail("expected payload match arm to return v + 1");
        }
    }

    {
        const std::string source = R"(enum E {
    A;
}
fn main() -> Int {
    match (E::A) {
        E::A => { return 42; }
    }
    return 0;
})";

        const auto chunk = compile_to_chunk(source);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected scoped enum name + match arm to run");
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
        caps.insert("io.stdout");
        const auto res = run_chunk_with_caps(chunk, caps);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected print(...) program to run with io.stdout capability");
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

    // Emitter should reject declaring the tail reserved builtin name.
    {
        const std::string source =
            "fn __set_insert_int() -> Int { return 0; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin set_insert case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin set_insert case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin set_insert case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__set_insert_int'") ==
                std::string::npos)
        {
            fail("expected builtin __set_insert_int diagnostic");
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

    // main cannot take non-capability parameters in runnable code.
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
            diags[0].message.find("main parameters are only supported") == std::string::npos)
        {
            fail("expected main params diagnostic");
        }
    }

    // main may take capability parameters in runnable code.
    {
        const std::string source = "fn helper(c: cap io.stdout) -> Unit { print(1); return; }"
                                   "fn main(c: cap io.stdout) -> Unit { helper(c); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for main cap params case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for main cap params case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<curlee::vm::Chunk>(emitted))
        {
            fail("expected emission to succeed for main cap params case");
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
        const std::string source_member = R"(struct Point {
    x: Int;
    y: Int;
}
fn main() -> Int {
    let p: Point = Point{x: 1, y: 2};
    return p.x + p.y;
})";

        const auto chunk = compile_to_chunk(source_member);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::MakeStruct) ||
            !contains_op(ops, curlee::vm::OpCode::GetField))
        {
            fail("expected struct literal/member access to emit struct opcodes");
        }

        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(3)))
        {
            fail("expected runnable struct literal/member access to evaluate");
        }
    }

    {
        const std::string source_scoped =
            "fn main() -> Int { match (a::b) { a::b => { return 7; } } return 0; }";

        const auto chunk = compile_to_chunk(source_scoped);
        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(7)))
        {
            fail("expected scoped enum constructor expression to be runnable");
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
        using curlee::parser::MemberExpr;
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
            if (diags.empty() || diags[0].message.find("unknown struct") == std::string::npos)
            {
                fail("expected struct literal diagnostic");
            }
        }

        {
            Expr root;
            root.node = MemberExpr{.base = nullptr, .member = "x"};
            const auto emitted = run_with_expr(std::move(root));
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for null member base case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("invalid member access in runnable code") ==
                    std::string::npos)
            {
                fail("expected invalid member access diagnostic");
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

    {
        // __rng_next_int should enforce arity in emitter.
        const std::string source = "fn main() -> Int { return __rng_next_int(10); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __rng_next_int argcount case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __rng_next_int argcount case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __rng_next_int argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__rng_next_int expects exactly 2 argument") ==
                std::string::npos)
        {
            fail("expected __rng_next_int argcount diagnostic");
        }
    }

    {
        // __rng_next_int should emit and run with rng.seeded capability.
        const std::string source =
            "fn main(r: cap rng.seeded) -> Int { return __rng_next_int(10, r); }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::RngNextInt))
        {
            fail("expected __rng_next_int(...) to emit RngNextInt opcode");
        }

        curlee::vm::VM::Capabilities caps;
        caps.insert("rng.seeded");
        const auto res = run_chunk_with_caps(chunk, caps);
        if (!res.ok || res.value.kind != curlee::vm::ValueKind::Int)
        {
            fail("expected __rng_next_int(...) run to produce Int result");
        }
    }

    {
        // __vec_* intrinsics should enforce arity in emitter.
        const std::string source =
            "fn main() -> Unit { __vec_new_int(); __vec_len_int(); __vec_push_int(1); "
            "__vec_get_int(1); __vec_set_int(1,2); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __vec_* argcount case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __vec_* argcount case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __vec_* argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__vec_new_int expects exactly 1 argument") ==
                std::string::npos)
        {
            fail("expected __vec_new_int argcount diagnostic");
        }
    }

    {
        // __vec_* intrinsics should emit and run end-to-end.
        const std::string source =
            "fn main() -> Int { let v: Int = __vec_new_int(4); __vec_push_int(v, 7); "
            "__vec_set_int(v, 0, 9); return __vec_get_int(v, 0) + __vec_len_int(v); }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::VecNew) ||
            !contains_op(ops, curlee::vm::OpCode::VecLen) ||
            !contains_op(ops, curlee::vm::OpCode::VecPush) ||
            !contains_op(ops, curlee::vm::OpCode::VecGet) ||
            !contains_op(ops, curlee::vm::OpCode::VecSet))
        {
            fail("expected __vec_* builtins to emit vec opcodes");
        }

        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(10)))
        {
            fail("expected __vec_* program to evaluate to 10");
        }
    }

    {
        // __vec_*_bool and __set_*_int intrinsics should emit and run end-to-end.
        const std::string source =
            "fn main() -> Int { let v: Int = __vec_new_bool(4); __vec_push_bool(v, true); "
            "__vec_set_bool(v, 0, false); let s: Int = __set_new_int(); __set_insert_int(s, "
            "9); if (__set_has_int(s, 9)) { if (__vec_get_bool(v, 0)) { return 0; } return "
            "__vec_len_bool(v); } return 0; }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::VecNewBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecLenBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecPushBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecGetBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecSetBool) ||
            !contains_op(ops, curlee::vm::OpCode::SetNewInt) ||
            !contains_op(ops, curlee::vm::OpCode::SetHasInt) ||
            !contains_op(ops, curlee::vm::OpCode::SetInsertInt))
        {
            fail("expected __vec_*_bool/__set_*_int builtins to emit new opcodes");
        }

        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(1)))
        {
            fail("expected bool vec + int set program to evaluate to 1");
        }
    }

    {
        // Exercise intrinsic dispatch branches for all runtime builtins.
        const std::string source =
            "fn main(i: cap io.stdin, r: cap fs.read, w: cap fs.write, t: cap io.tty, g: cap "
            "rng.seeded) -> Unit { let v: Int = __vec_new_int(2); let vb: Int = __vec_new_bool(2); "
            "let s: Int = __set_new_int(); __read_line(i); "
            "__fs_read_text(\"p\", r); __fs_write_text(\"p\", \"x\", w); "
            "__tty_clear(t); __tty_write_at(0, 0, \"x\", t); __tty_flush(t); "
            "__rng_next_int(10, g); __vec_len_int(v); __vec_push_int(v, 1); "
            "__vec_get_int(v, 0); __vec_set_int(v, 0, 1); "
            "__vec_len_bool(vb); __vec_push_bool(vb, true); __vec_get_bool(vb, 0); "
            "__vec_set_bool(vb, 0, false); __set_has_int(s, 1); __set_insert_int(s, 1); "
            "return; }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::ReadLine) ||
            !contains_op(ops, curlee::vm::OpCode::FsReadText) ||
            !contains_op(ops, curlee::vm::OpCode::FsWriteText) ||
            !contains_op(ops, curlee::vm::OpCode::TtyClear) ||
            !contains_op(ops, curlee::vm::OpCode::TtyWriteAt) ||
            !contains_op(ops, curlee::vm::OpCode::TtyFlush) ||
            !contains_op(ops, curlee::vm::OpCode::RngNextInt) ||
            !contains_op(ops, curlee::vm::OpCode::VecNew) ||
            !contains_op(ops, curlee::vm::OpCode::VecLen) ||
            !contains_op(ops, curlee::vm::OpCode::VecPush) ||
            !contains_op(ops, curlee::vm::OpCode::VecGet) ||
            !contains_op(ops, curlee::vm::OpCode::VecSet) ||
            !contains_op(ops, curlee::vm::OpCode::VecNewBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecLenBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecPushBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecGetBool) ||
            !contains_op(ops, curlee::vm::OpCode::VecSetBool) ||
            !contains_op(ops, curlee::vm::OpCode::SetNewInt) ||
            !contains_op(ops, curlee::vm::OpCode::SetHasInt) ||
            !contains_op(ops, curlee::vm::OpCode::SetInsertInt))
        {
            fail("expected intrinsic dispatch program to emit all runtime opcodes");
        }
    }

    {
        // Intrinsic argument emission errors should propagate through emit_intrinsic.
        const std::string source = "fn main(i: cap io.stdin) -> Unit { __read_line(missing); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for intrinsic argument diag case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for intrinsic argument diag case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for intrinsic argument diag case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from intrinsic argument emission");
        }
    }

    {
        // variant predicate should reject non-tag second argument.
        const std::string source =
            "enum E { A(Int); } fn main() -> Bool { let e: Int = E::A(1); return variant_is(e, e); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_is second-arg diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_is second-arg diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_is second-arg diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("variant_is expects enum variant tag as second argument") ==
                std::string::npos)
        {
            fail("expected variant_is second-argument diagnostic");
        }
    }

    {
        // variant predicate should propagate diagnostics from first argument emission.
        const std::string source =
            "enum E { A(Int); } fn main() -> Bool { return variant_is(missing, E::A); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_is first-arg diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_is first-arg diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_is first-arg diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from variant_is first argument");
        }
    }

    {
        // variant predicate should reject wrong arity.
        const std::string source =
            "enum E { A(Int); } fn main() -> Bool { let e: Int = E::A(1); return variant_is(e); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_is arity diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_is arity diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_is arity diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("variant_is expects exactly 2 argument(s)") == std::string::npos)
        {
            fail("expected variant_is arity diagnostic");
        }
    }

    {
        // variant_unwrap should route through variant predicate arity handling.
        const std::string source =
            "enum E { A(Int); } fn main() -> Int { let e: Int = E::A(1); return variant_unwrap(e); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_unwrap arity diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_unwrap arity diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_unwrap arity diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("variant_unwrap expects exactly 2 argument(s)") ==
                std::string::npos)
        {
            fail("expected variant_unwrap arity diagnostic");
        }
    }

    {
        // Scoped enum constructor should reject >1 payload args.
        const std::string source = "enum E { A(Int); } fn main() -> Int { let e: Int = E::A(1, 2); return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for scoped constructor arity diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for scoped constructor arity diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for scoped constructor arity diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("enum constructors in runnable code accept at most one payload argument") ==
                std::string::npos)
        {
            fail("expected scoped constructor arity diagnostic");
        }
    }

    {
        // Scoped enum constructor should propagate payload-expression diagnostics.
        const std::string source =
            "enum E { A(Int); } fn main() -> Int { let e: Int = E::A(missing); return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for scoped constructor payload diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for scoped constructor payload diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for scoped constructor payload diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from scoped constructor payload expression");
        }
    }

    {
        // Scoped enum constructor call with no payload should take the empty-args path.
        const std::string source = "enum E { A; } fn main() -> Int { let e: Int = E::A(); return 0; }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::MakeEnum))
        {
            fail("expected scoped zero-arg constructor call to emit MakeEnum");
        }

        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected scoped zero-arg constructor program to run and return 0");
        }
    }

    {
        // Match arm payload bindings should reject duplicate names already in-scope.
        const std::string source = R"(enum Maybe {
    Some(Int);
    None;
}
fn main() -> Int {
    let x: Int = 0;
    let m: Int = Maybe::Some(1);
    match (m) {
        Maybe::Some(x) => { return x; }
        Maybe::None => { return 0; }
    }
    return -1;
})";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for duplicate match payload binding diag");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for duplicate match payload binding diag");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for duplicate match payload binding diag");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("duplicate match payload binding") ==
                                 std::string::npos)
        {
            fail("expected duplicate match payload binding diagnostic");
        }
    }

    {
        // Match expression should propagate diagnostics from scrutinee expression emission.
        const std::string source = R"(enum E {
    A;
}
fn main() -> Int {
    match (missing) {
        E::A => { return 0; }
    }
    return -1;
})";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for match scrutinee diag case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for match scrutinee diag case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for match scrutinee diag case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from match scrutinee emission");
        }
    }

    {
        // Match arm body emission should propagate nested statement diagnostics.
        const std::string source = R"(enum E {
    A;
}
fn main() -> Int {
    match (E::A) {
        E::A => { missing; return 0; }
    }
    return -1;
})";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for match arm body diag case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for match arm body diag case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for match arm body diag case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from match arm body emission");
        }
    }

    {
        // Force match arm body null branch via manual AST shaping.
        using curlee::parser::MatchStmt;

        const std::string source = R"(enum E {
    A;
}
fn main() -> Int {
    match (E::A) {
        E::A => { return 0; }
    }
    return -1;
})";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for null match arm body case");
        }
        auto parsed = curlee::parser::parse(std::get<std::vector<curlee::lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for null match arm body case");
        }

        auto program = std::get<curlee::parser::Program>(std::move(parsed));
        auto* match_stmt = std::get_if<MatchStmt>(&program.functions[0].body.stmts[0].node);
        if (match_stmt == nullptr || match_stmt->arms.empty())
        {
            fail("expected parsed match statement with at least one arm");
        }
        match_stmt->arms[0].body.reset();

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for null match arm body case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("match arm body missing in runnable code") ==
                                 std::string::npos)
        {
            fail("expected match-arm-body-missing diagnostic");
        }
    }

    {
        // Duplicate struct declarations should be rejected by emitter symbol collection.
        const std::string source =
            "struct Point { x: Int; } struct Point { y: Int; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for duplicate struct declaration case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for duplicate struct declaration case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for duplicate struct declaration case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("duplicate struct declaration 'Point'") == std::string::npos)
        {
            fail("expected duplicate struct declaration diagnostic");
        }
    }

    {
        // Member access should propagate base-expression diagnostics.
        const std::string source = "fn main() -> Int { return missing.x; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for member-base diagnostic case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for member-base diagnostic case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for member-base diagnostic case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from member-base emission");
        }
    }

    {
        // Struct literal should reject duplicate field names.
        // Parser rejects duplicate fields, so mutate a valid AST to force emitter branch.
        using curlee::parser::LetStmt;
        using curlee::parser::StructLiteralExpr;

        const std::string source =
            "struct Point { x: Int; y: Int; } fn main() -> Int { let p: Point = Point{x: 1, y: 2}; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for duplicate struct field case");
        }
        auto parsed = curlee::parser::parse(std::get<std::vector<curlee::lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for duplicate struct field case");
        }

        auto program = std::get<curlee::parser::Program>(std::move(parsed));
        auto* let_stmt = std::get_if<LetStmt>(&program.functions[0].body.stmts[0].node);
        if (let_stmt == nullptr)
        {
            fail("expected let init expression in duplicate struct field case");
        }
        auto* struct_lit = std::get_if<StructLiteralExpr>(&let_stmt->value.node);
        if (struct_lit == nullptr || struct_lit->fields.size() < 2)
        {
            fail("expected struct literal expression in duplicate struct field case");
        }
        struct_lit->fields[1].name = struct_lit->fields[0].name;

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for duplicate struct field case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("duplicate field 'x' in struct literal") ==
                                 std::string::npos)
        {
            fail("expected duplicate field diagnostic");
        }
    }

    {
        // Struct literal should reject unknown fields.
        const std::string source =
            "struct Point { x: Int; y: Int; } fn main() -> Int { let p: Point = Point{x: 1, z: 2}; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unknown struct field case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unknown struct field case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unknown struct field case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("unknown field 'z' for struct 'Point'") == std::string::npos)
        {
            fail("expected unknown field diagnostic");
        }
    }

    {
        // Struct literal should reject incorrect field counts.
        const std::string source =
            "struct Point { x: Int; y: Int; } fn main() -> Int { let p: Point = Point{x: 1}; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for struct field-count case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for struct field-count case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for struct field-count case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("struct literal for 'Point' has incorrect field count") ==
                std::string::npos)
        {
            fail("expected struct field-count diagnostic");
        }
    }

    {
        // Force null struct field value branch via manual AST shaping.
        using curlee::parser::LetStmt;
        using curlee::parser::NameExpr;
        using curlee::parser::StructLiteralExpr;

        const std::string source =
            "struct Point { x: Int; } fn main() -> Int { let p: Point = Point{x: 1}; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for null struct field-value case");
        }
        auto parsed = curlee::parser::parse(std::get<std::vector<curlee::lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for null struct field-value case");
        }

        auto program = std::get<curlee::parser::Program>(std::move(parsed));
        auto* let_stmt = std::get_if<LetStmt>(&program.functions[0].body.stmts[0].node);
        if (let_stmt == nullptr)
        {
            fail("expected let init expression in null struct field-value case");
        }
        auto* struct_lit = std::get_if<StructLiteralExpr>(&let_stmt->value.node);
        if (struct_lit == nullptr || struct_lit->fields.empty())
        {
            fail("expected struct literal expression in null struct field-value case");
        }
        struct_lit->fields[0].value.reset();

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for null struct field-value case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("missing struct field value in runnable code") ==
                std::string::npos)
        {
            fail("expected missing struct field value diagnostic");
        }
    }

    {
        // Force field-expression emission failure branch in struct literal lowering.
        using curlee::parser::Expr;
        using curlee::parser::LetStmt;
        using curlee::parser::NameExpr;
        using curlee::parser::StructLiteralExpr;

        const std::string source =
            "struct Point { x: Int; } fn main() -> Int { let p: Point = Point{x: 1}; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for struct field-expression failure case");
        }
        auto parsed = curlee::parser::parse(std::get<std::vector<curlee::lexer::Token>>(lexed));
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for struct field-expression failure case");
        }

        auto program = std::get<curlee::parser::Program>(std::move(parsed));
        auto* let_stmt = std::get_if<LetStmt>(&program.functions[0].body.stmts[0].node);
        if (let_stmt == nullptr)
        {
            fail("expected let init expression in struct field-expression failure case");
        }
        auto* struct_lit = std::get_if<StructLiteralExpr>(&let_stmt->value.node);
        if (struct_lit == nullptr || struct_lit->fields.empty())
        {
            fail("expected struct literal expression in struct field-expression failure case");
        }

        auto invalid_expr = std::make_unique<Expr>();
        invalid_expr->span = struct_lit->fields[0].span;
        invalid_expr->node = NameExpr{.name = "missing"};
        struct_lit->fields[0].value = std::move(invalid_expr);

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for struct field-expression failure case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'missing'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic from struct field-expression failure");
        }
    }

    {
        // Force emit_string_constant overflow from member field emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::MemberExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
        main_fn.body.stmts.reserve(prefill_count + 1);

        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};

            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        Expr base_expr;
        base_expr.span = {};
        base_expr.node = IntExpr{.lexeme = "1"};

        Expr member_expr;
        member_expr.span = {};
        member_expr.node = MemberExpr{.base = std::make_unique<Expr>(std::move(base_expr)),
                                      .member = "overflow_field"};

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(member_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for member field constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("struct field constant index overflow") == std::string::npos)
        {
            fail("expected struct field constant index overflow diagnostic");
        }
    }

    {
        // Force enum variant-name constant overflow in scoped constructor emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::ScopedNameExpr;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());
        main_fn.body.stmts.reserve(prefill_count + 1);

        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};

            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        Expr scoped_expr;
        scoped_expr.span = {};
        scoped_expr.node = ScopedNameExpr{.lhs = "E", .rhs = "A"};

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(scoped_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for enum variant constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("enum variant constant index overflow") == std::string::npos)
        {
            fail("expected enum variant constant index overflow diagnostic");
        }
    }

    {
        // Force enum-name constant overflow in scoped constructor emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::ScopedNameExpr;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
        main_fn.body.stmts.reserve(prefill_count + 1);

        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};

            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        Expr scoped_expr;
        scoped_expr.span = {};
        scoped_expr.node = ScopedNameExpr{.lhs = "E", .rhs = "A"};

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(scoped_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for enum name constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("enum name constant index overflow") == std::string::npos)
        {
            fail("expected enum name constant index overflow diagnostic");
        }
    }

    {
        // Force struct-name constant overflow in struct literal emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::StructDecl;
        using curlee::parser::StructLiteralExpr;
        using curlee::parser::TypeName;

        Program program;
        program.structs.push_back(StructDecl{.span = {}, .name = "Point", .fields = {}});

        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
        main_fn.body.stmts.reserve(prefill_count + 1);

        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};

            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        Expr literal_expr;
        literal_expr.span = {};
        literal_expr.node = StructLiteralExpr{.type_name = "Point", .fields = {}};

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(literal_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for struct name constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("struct name constant index overflow") == std::string::npos)
        {
            fail("expected struct name constant index overflow diagnostic");
        }
    }

    {
        // Force struct field-name constant overflow in MakeStruct field-index loop.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::StructDecl;
        using curlee::parser::StructDeclField;
        using curlee::parser::StructLiteralExpr;
        using curlee::parser::StructLiteralExprField;
        using curlee::parser::TypeName;

        Program program;
        program.structs.push_back(StructDecl{.span = {},
                                             .name = "Point",
                                             .fields = {StructDeclField{.span = {},
                                                                        .name = "x",
                                                                        .type = TypeName{.span = {},
                                                                                         .name = "Int"}}}});

        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - 1;
        main_fn.body.stmts.reserve(prefill_count + 1);

        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};

            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        Expr field_value;
        field_value.span = {};
        field_value.node = IntExpr{.lexeme = "1"};

        StructLiteralExpr literal_node;
        literal_node.type_name = "Point";
        StructLiteralExprField only_field;
        only_field.span = {};
        only_field.name = "x";
        only_field.value = std::make_unique<Expr>(std::move(field_value));
        literal_node.fields.push_back(std::move(only_field));

        Expr literal_expr;
        literal_expr.span = {};
        literal_expr.node = std::move(literal_node);

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(literal_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));

        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for struct field constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("struct field constant index overflow") == std::string::npos)
        {
            fail("expected struct field constant index overflow diagnostic in MakeStruct loop");
        }
    }

    {
        // Force collect_member_chain fallback on non-name/non-member call qualifier base.
        using curlee::parser::BinaryExpr;
        using curlee::parser::CallExpr;
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::MemberExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        Expr lhs;
        lhs.span = {};
        lhs.node = IntExpr{.lexeme = "1"};

        Expr rhs;
        rhs.span = {};
        rhs.node = IntExpr{.lexeme = "2"};

        Expr qualifier_base;
        qualifier_base.span = {};
        qualifier_base.node = BinaryExpr{.op = curlee::lexer::TokenKind::Plus,
                                         .lhs = std::make_unique<Expr>(std::move(lhs)),
                                         .rhs = std::make_unique<Expr>(std::move(rhs))};

        Expr callee_expr;
        callee_expr.span = {};
        callee_expr.node = MemberExpr{.base = std::make_unique<Expr>(std::move(qualifier_base)),
                                      .member = "f"};

        Expr call_root;
        call_root.span = {};
        CallExpr call;
        call.callee = std::make_unique<Expr>(std::move(callee_expr));
        call_root.node = std::move(call);

        Stmt call_stmt;
        call_stmt.span = {};
        call_stmt.node = ExprStmt{.expr = std::move(call_root)};
        main_fn.body.stmts.push_back(std::move(call_stmt));

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = Expr{.span = {}, .node = IntExpr{.lexeme = "0"}}};
        main_fn.body.stmts.push_back(std::move(ret_stmt));
        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for unsupported module-qualified call base");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("only name calls and module-qualified calls are supported") ==
                std::string::npos)
        {
            fail("expected module-qualified call fallback diagnostic");
        }
    }

    {
        // Force match-enum constant overflow to hit early return after enum constant emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::MatchArm;
        using curlee::parser::MatchArmPattern;
        using curlee::parser::MatchStmt;
        using curlee::parser::Program;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());
        main_fn.body.stmts.reserve(prefill_count + 1);
        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};
            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        MatchStmt match_stmt;
        match_stmt.value.span = {};
        match_stmt.value.node = IntExpr{.lexeme = "1"};

        MatchArm arm;
        arm.span = {};
        arm.pattern = MatchArmPattern{.span = {}, .enum_name = "E", .variant_name = "A"};
        arm.body = std::make_unique<curlee::parser::Block>();
        arm.body->span = {};
        match_stmt.arms.push_back(std::move(arm));

        Stmt match_stmt_node;
        match_stmt_node.span = {};
        match_stmt_node.node = std::move(match_stmt);
        main_fn.body.stmts.push_back(std::move(match_stmt_node));
        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for match enum constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("match enum constant index overflow") == std::string::npos)
        {
            fail("expected match enum constant index overflow diagnostic");
        }
    }

    {
        // Force match-variant constant overflow to hit early return after variant constant emission.
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::IntExpr;
        using curlee::parser::MatchArm;
        using curlee::parser::MatchArmPattern;
        using curlee::parser::MatchStmt;
        using curlee::parser::Program;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;

        Program program;
        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        const std::size_t prefill_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - 1;
        main_fn.body.stmts.reserve(prefill_count + 1);
        for (std::size_t i = 0; i < prefill_count; ++i)
        {
            Expr e;
            e.span = {};
            e.node = IntExpr{.lexeme = "0"};
            Stmt s;
            s.span = {};
            s.node = ExprStmt{.expr = std::move(e)};
            main_fn.body.stmts.push_back(std::move(s));
        }

        MatchStmt match_stmt;
        match_stmt.value.span = {};
        match_stmt.value.node = IntExpr{.lexeme = "1"};

        MatchArm arm;
        arm.span = {};
        arm.pattern = MatchArmPattern{.span = {}, .enum_name = "E", .variant_name = "A"};
        arm.body = std::make_unique<curlee::parser::Block>();
        arm.body->span = {};
        match_stmt.arms.push_back(std::move(arm));

        Stmt match_stmt_node;
        match_stmt_node.span = {};
        match_stmt_node.node = std::move(match_stmt);
        main_fn.body.stmts.push_back(std::move(match_stmt_node));
        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for match variant constant overflow case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("match variant constant index overflow") == std::string::npos)
        {
            fail("expected match variant constant index overflow diagnostic");
        }
    }

    {
        // Force struct field-count >16-bit guard before MakeStruct emission.
        using curlee::parser::Expr;
        using curlee::parser::Function;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::StructDecl;
        using curlee::parser::StructDeclField;
        using curlee::parser::StructLiteralExpr;
        using curlee::parser::StructLiteralExprField;
        using curlee::parser::TypeName;

        Program program;
        std::vector<std::string> field_names;

        StructDecl decl;
        decl.span = {};
        decl.name = "Huge";
        const std::size_t huge_count =
            static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
        field_names.reserve(huge_count);
        decl.fields.reserve(huge_count);
        for (std::size_t i = 0; i < huge_count; ++i)
        {
            field_names.push_back("f" + std::to_string(i));
            decl.fields.push_back(StructDeclField{.span = {},
                                                  .name = field_names.back(),
                                                  .type = TypeName{.span = {}, .name = "Int"}});
        }
        program.structs.push_back(std::move(decl));

        Function main_fn;
        main_fn.span = {};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = {}, .name = "Int"};

        StructLiteralExpr lit;
        lit.type_name = "Huge";
        lit.fields.reserve(huge_count);
        for (std::size_t i = 0; i < huge_count; ++i)
        {
            StructLiteralExprField field;
            field.span = {};
            field.name = field_names[i];
            lit.fields.push_back(std::move(field));
        }

        Expr root_expr;
        root_expr.span = {};
        root_expr.node = std::move(lit);

        Stmt ret_stmt;
        ret_stmt.span = {};
        ret_stmt.node = ReturnStmt{.value = std::move(root_expr)};
        main_fn.body.stmts.push_back(std::move(ret_stmt));
        program.functions.push_back(std::move(main_fn));

        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for oversized struct field-count case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("struct field count exceeds 16-bit runnable bytecode limit") ==
                std::string::npos)
        {
            fail("expected oversized struct field-count diagnostic");
        }
    }

    std::cout << "OK\n";
    return 0;
}
