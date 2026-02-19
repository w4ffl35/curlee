#include <cstdlib>
#include <curlee/compiler/emitter.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/vm/vm.h>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
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
        case OpCode::MakeEnum:
            ip += 5;
            break;
        case OpCode::EnumIs:
        case OpCode::EnumUnwrap:
            ip += 4;
            break;
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
        caps.insert("io.stdout");
        const auto res = run_chunk_with_caps(chunk, caps);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected print(...) program to run with io.stdout capability");
        }
    }

    {
        const std::string source =
            "enum Maybe { Some(Int); None; }"
            "fn main() -> Int {"
            "  let m: Maybe = Maybe::Some(41);"
            "  if (variant_is(m, Maybe::Some)) {"
            "    return variant_unwrap(m, Maybe::Some) + 1;"
            "  }"
            "  return 0;"
            "}";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::MakeEnum) ||
            !contains_op(ops, curlee::vm::OpCode::EnumIs) ||
            !contains_op(ops, curlee::vm::OpCode::EnumUnwrap))
        {
            fail("expected enum constructor and variant ops to emit enum opcodes");
        }

        const auto res = run_chunk(chunk);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(42)))
        {
            fail("expected enum variant test/unwrap path to evaluate to 42");
        }
    }

    {
        const std::string source = "fn main() -> Bool { return variant_is(1, nope); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_is bad-arg case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_is bad-arg case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_is bad-arg case");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("variant_is expects second argument Enum::Variant") ==
                std::string::npos)
        {
            fail("expected variant_is second-argument diagnostic");
        }
    }

    {
        const std::string source = "fn main() -> Int { return variant_unwrap(1); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_unwrap arity case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_unwrap arity case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_unwrap arity case");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("variant_unwrap expects exactly 2 arguments") ==
                std::string::npos)
        {
            fail("expected variant_unwrap arity diagnostic");
        }
    }

    {
        const std::string source =
            "enum E { A(Int); } fn main() -> E { return E::A(1, 2); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for enum constructor arity case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for enum constructor arity case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for enum constructor arity case");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("enum variant constructor expects at most 1 argument") ==
                std::string::npos)
        {
            fail("expected enum constructor arity diagnostic");
        }
    }

    {
        const std::string source = "enum E { A(Int); } fn main() -> E { return E::A(nope); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for enum constructor bad-arg name case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for enum constructor bad-arg name case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for enum constructor bad-arg name case");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for enum constructor argument");
        }
    }

    {
        const std::string source = "fn main() -> Bool { return variant_is(nope, E::A); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for variant_is bad first arg case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for variant_is bad first arg case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for variant_is bad first arg case");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for variant_is first argument");
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

    // Emitter should reject declaring builtin __read_line.
    {
        const std::string source =
            "fn __read_line(in: cap io.stdin) -> String { return \"\"; } fn main() -> Int { "
            "return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __read_line case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __read_line case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __read_line case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("cannot declare builtin function '__read_line'") ==
                                 std::string::npos)
        {
            fail("expected builtin __read_line diagnostic");
        }
    }

    // Emitter should reject declaring builtin __tty_clear.
    {
        const std::string source =
            "fn __tty_clear(tty: cap io.tty) -> Unit { return 0; } fn main() -> Int { "
            "return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __tty_clear case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __tty_clear case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __tty_clear case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("cannot declare builtin function '__tty_clear'") ==
                                 std::string::npos)
        {
            fail("expected builtin __tty_clear diagnostic");
        }
    }

    // Emitter should reject declaring builtin __fs_read_text.
    {
        const std::string source =
            "fn __fs_read_text(path: String, r: cap fs.read) -> String { return path; } fn "
            "main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __fs_read_text case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __fs_read_text case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __fs_read_text case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__fs_read_text'") ==
                std::string::npos)
        {
            fail("expected builtin __fs_read_text diagnostic");
        }
    }

    // Emitter should reject declaring builtin __fs_write_text.
    {
        const std::string source =
            "fn __fs_write_text(path: String, content: String, w: cap fs.write) -> Unit { "
            "return; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __fs_write_text case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __fs_write_text case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __fs_write_text case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__fs_write_text'") ==
                std::string::npos)
        {
            fail("expected builtin __fs_write_text diagnostic");
        }
    }

    // Emitter should reject declaring builtin __tty_write_at.
    {
        const std::string source =
            "fn __tty_write_at(row: Int, col: Int, text: String, tty: cap io.tty) -> Unit { "
            "return 0; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __tty_write_at case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __tty_write_at case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __tty_write_at case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__tty_write_at'") ==
                std::string::npos)
        {
            fail("expected builtin __tty_write_at diagnostic");
        }
    }

    // Emitter should reject declaring builtin __tty_flush.
    {
        const std::string source =
            "fn __tty_flush(tty: cap io.tty) -> Unit { return 0; } fn main() -> Int { return 0; "
            "}";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __tty_flush case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __tty_flush case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __tty_flush case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("cannot declare builtin function '__tty_flush'") ==
                                 std::string::npos)
        {
            fail("expected builtin __tty_flush diagnostic");
        }
    }

    // Emitter should reject declaring builtin __rng_next_int.
    {
        const std::string source =
            "fn __rng_next_int(max_exclusive: Int, rng: cap rng.seeded) -> Int { return 0; } "
            "fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __rng_next_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __rng_next_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __rng_next_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__rng_next_int'") ==
                std::string::npos)
        {
            fail("expected builtin __rng_next_int diagnostic");
        }
    }

    // Emitter should reject declaring builtin __vec_new_int.
    {
        const std::string source =
            "fn __vec_new_int(max_len: Int) -> Vec { return __vec_new_int(max_len); } "
            "fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __vec_new_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __vec_new_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __vec_new_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__vec_new_int'") ==
                std::string::npos)
        {
            fail("expected builtin __vec_new_int diagnostic");
        }
    }

    // Emitter should reject declaring builtin __vec_len_int.
    {
        const std::string source =
            "fn __vec_len_int(v: Vec) -> Int { return 0; } fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __vec_len_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __vec_len_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __vec_len_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__vec_len_int'") ==
                std::string::npos)
        {
            fail("expected builtin __vec_len_int diagnostic");
        }
    }

    // Emitter should reject declaring builtin __vec_push_int.
    {
        const std::string source =
            "fn __vec_push_int(v: Vec, x: Int) -> Unit { return; } fn main() -> Int { return 0; "
            "}";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __vec_push_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __vec_push_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __vec_push_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__vec_push_int'") ==
                std::string::npos)
        {
            fail("expected builtin __vec_push_int diagnostic");
        }
    }

    // Emitter should reject declaring builtin __vec_get_int.
    {
        const std::string source =
            "fn __vec_get_int(v: Vec, i: Int) -> Int { return 0; } fn main() -> Int { return 0; "
            "}";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __vec_get_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __vec_get_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __vec_get_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__vec_get_int'") ==
                std::string::npos)
        {
            fail("expected builtin __vec_get_int diagnostic");
        }
    }

    // Emitter should reject declaring builtin __vec_set_int.
    {
        const std::string source =
            "fn __vec_set_int(v: Vec, i: Int, x: Int) -> Unit { return; } "
            "fn main() -> Int { return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for builtin __vec_set_int case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for builtin __vec_set_int case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for builtin __vec_set_int case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("cannot declare builtin function '__vec_set_int'") ==
                std::string::npos)
        {
            fail("expected builtin __vec_set_int diagnostic");
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

    // Builtin __read_line should enforce argument count.
    {
        const std::string source = "fn main() -> String { return __read_line(); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __read_line argcount case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __read_line argcount case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __read_line argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__read_line expects exactly 1 argument") == std::string::npos)
        {
            fail("expected __read_line argcount diagnostic");
        }
    }

    // Builtin __read_line should report argument expression errors.
    {
        const std::string source = "fn main() -> String { return __read_line(nope); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __read_line bad-arg case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __read_line bad-arg case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __read_line bad-arg case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __read_line argument");
        }
    }

    {
        const std::string source =
            "fn main(in: cap io.stdin) -> String { return __read_line(in); }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::ReadLine))
        {
            fail("expected __read_line(...) to emit ReadLine opcode");
        }

        std::istringstream in("hello from stdin\n");
        std::streambuf* old_in = std::cin.rdbuf(in.rdbuf());
        std::cin.clear();

        curlee::vm::VM::Capabilities caps;
        caps.insert("io.stdin");
        const auto res = run_chunk_with_caps(chunk, caps);

        std::cin.rdbuf(old_in);

        if (!res.ok)
        {
            fail("expected __read_line(...) run to succeed; error=" + res.error);
        }
        if (!(res.value == curlee::vm::Value::string_v("hello from stdin")))
        {
            fail("expected __read_line(...) to return 'hello from stdin', got=" +
                 curlee::vm::to_string(res.value));
        }
    }

    // Builtin __fs_read_text should enforce argument count.
    {
        const std::string source =
            "fn main(r: cap fs.read) -> String { return __fs_read_text(r); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_read_text argcount case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_read_text argcount case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_read_text argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__fs_read_text expects exactly 2 arguments") ==
                std::string::npos)
        {
            fail("expected __fs_read_text argcount diagnostic");
        }
    }

    // Builtin __fs_write_text should enforce argument count.
    {
        const std::string source =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a.txt\", w); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_write_text argcount case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_write_text argcount case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_write_text argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__fs_write_text expects exactly 3 arguments") ==
                std::string::npos)
        {
            fail("expected __fs_write_text argcount diagnostic");
        }
    }

    // Builtin __fs_read_text should report argument expression errors.
    {
        const std::string source =
            "fn main(r: cap fs.read) -> String { return __fs_read_text(nope, r); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_read_text bad-arg case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_read_text bad-arg case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_read_text bad-arg case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __fs_read_text argument");
        }
    }

    {
        const std::string source =
            "fn main() -> String { return __fs_read_text(\"in.txt\", nope); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_read_text bad-cap case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_read_text bad-cap case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_read_text bad-cap case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __fs_read_text capability argument");
        }
    }

    {
        const std::string source =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(nope, \"x\", w); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_write_text first-arg error case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_write_text first-arg error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_write_text first-arg error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __fs_write_text first argument");
        }
    }

    {
        const std::string source =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a.txt\", nope, w); "
            "return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_write_text second-arg error case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_write_text second-arg error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_write_text second-arg error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __fs_write_text second argument");
        }
    }

    {
        const std::string source =
            "fn main() -> Unit { __fs_write_text(\"a.txt\", \"x\", nope); return; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __fs_write_text third-arg error case");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __fs_write_text third-arg error case");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __fs_write_text third-arg error case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __fs_write_text third argument");
        }
    }

    {
        const std::string source =
            "fn main(r: cap fs.read, w: cap fs.write) -> Unit { let text: String = "
            "__fs_read_text(\"in.txt\", r); __fs_write_text(\"out.txt\", text, w); return; }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::FsReadText) ||
            !contains_op(ops, curlee::vm::OpCode::FsWriteText))
        {
            fail("expected __fs_* builtins to emit fs opcodes");
        }
    }

    // Builtin __tty_clear should enforce argument count.
    {
        const std::string source = "fn main() -> Unit { __tty_clear(); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_clear argcount case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_clear argcount case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_clear argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__tty_clear expects exactly 1 argument") == std::string::npos)
        {
            fail("expected __tty_clear argcount diagnostic");
        }
    }

    // Builtin __tty_write_at should enforce argument count.
    {
        const std::string source =
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, 0, \"x\"); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_write_at argcount case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_write_at argcount case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_write_at argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("__tty_write_at expects exactly 4 arguments") ==
                                 std::string::npos)
        {
            fail("expected __tty_write_at argcount diagnostic");
        }
    }

    // Builtin __tty_flush should enforce argument count.
    {
        const std::string source = "fn main() -> Unit { __tty_flush(); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_flush argcount case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_flush argcount case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_flush argcount case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() ||
            diags[0].message.find("__tty_flush expects exactly 1 argument") == std::string::npos)
        {
            fail("expected __tty_flush argcount diagnostic");
        }
    }

    // Builtin __tty_write_at should report argument expression errors.
    {
        const std::string source =
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(nope, 0, \"x\", tty); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_write_at bad-arg case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_write_at bad-arg case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_write_at bad-arg case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __tty_write_at argument");
        }
    }

    // Builtin __tty_clear should report argument expression errors.
    {
        const std::string source = "fn main() -> Unit { __tty_clear(nope); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_clear bad-arg case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_clear bad-arg case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_clear bad-arg case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __tty_clear argument");
        }
    }

    // Builtin __tty_flush should report argument expression errors.
    {
        const std::string source = "fn main() -> Unit { __tty_flush(nope); return; }";
        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for __tty_flush bad-arg case");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for __tty_flush bad-arg case");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto emitted = curlee::compiler::emit_bytecode(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
        {
            fail("expected emission to fail for __tty_flush bad-arg case");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
        if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
        {
            fail("expected unknown-name diagnostic for __tty_flush argument");
        }
    }

    // Builtin __rng_next_int should report arity and argument expression errors.
    {
        {
            const std::string source =
                "fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(10, rng, rng); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __rng_next_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __rng_next_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __rng_next_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__rng_next_int expects exactly 2 arguments") ==
                    std::string::npos)
            {
                fail("expected __rng_next_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(nope, rng); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __rng_next_int first-arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __rng_next_int first-arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __rng_next_int first-arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __rng_next_int first argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __rng_next_int(10, nope); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __rng_next_int second-arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __rng_next_int second-arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __rng_next_int second-arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __rng_next_int second argument");
            }
        }
    }

    {
        const std::string source =
            "fn main(tty: cap io.tty) -> Int { __tty_clear(tty); __tty_write_at(1, 2, \"x\", "
            "tty); __tty_flush(tty); return 0; }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::TtyClear) ||
            !contains_op(ops, curlee::vm::OpCode::TtyWriteAt) ||
            !contains_op(ops, curlee::vm::OpCode::TtyFlush))
        {
            fail("expected __tty_* builtins to emit tty opcodes");
        }

        curlee::vm::VM::Capabilities caps;
        caps.insert("io.tty");
        const auto res = run_chunk_with_caps(chunk, caps);
        if (!res.ok || !(res.value == curlee::vm::Value::int_v(0)))
        {
            fail("expected __tty_* program to run with io.tty capability");
        }
    }

    {
        const std::string source =
            "fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(100, rng); }";

        const auto chunk = compile_to_chunk(source);
        const auto ops = decode_ops(chunk);
        if (!contains_op(ops, curlee::vm::OpCode::RngNextInt))
        {
            fail("expected __rng_next_int to emit RngNextInt opcode");
        }

        curlee::vm::VM::Capabilities caps;
        caps.insert("rng.seeded");
        curlee::vm::VM vm_a;
        const auto a = vm_a.run(chunk, std::numeric_limits<std::size_t>::max(), caps,
                                std::uint64_t{42});
        if (!a.ok || a.value.kind != curlee::vm::ValueKind::Int)
        {
            fail("expected __rng_next_int program to run with rng.seeded capability and seed");
        }

        curlee::vm::VM vm_b;
        const auto b = vm_b.run(chunk, std::numeric_limits<std::size_t>::max(), caps,
                                std::uint64_t{42});
        if (!b.ok || !(a.value == b.value))
        {
            fail("expected deterministic __rng_next_int result for same seed");
        }
    }

    // Builtin __vec_* should report arity and argument expression errors, and emit vec opcodes.
    {
        {
            const std::string source =
                "fn main() -> Unit { __vec_new_int(1, 2); return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_new_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_new_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_new_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__vec_new_int expects exactly 1 argument") ==
                    std::string::npos)
            {
                fail("expected __vec_new_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __vec_len_int(__vec_new_int(nope)); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_new_int arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_new_int arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_new_int arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_new_int argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { __vec_push_int(nope, 1); return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_push_int vec arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_push_int vec arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_push_int vec arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_push_int vec argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { let v: Vec = __vec_new_int(2); __vec_push_int(v, nope); "
                "return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_push_int value arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_push_int value arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_push_int value arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_push_int value argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { let v: Vec = __vec_new_int(2); __vec_set_int(v, nope, 1); "
                "return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_set_int index arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_set_int index arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_set_int index arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_set_int index argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { let v: Vec = __vec_new_int(2); __vec_set_int(v, 0, nope); "
                "return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_set_int value arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_set_int value arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_set_int value arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_set_int value argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __vec_len_int(); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_len_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_len_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_len_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__vec_len_int expects exactly 1 argument") ==
                    std::string::npos)
            {
                fail("expected __vec_len_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __vec_len_int(nope); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_len_int arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_len_int arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_len_int arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_len_int argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { __vec_push_int(__vec_new_int(2)); return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_push_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_push_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_push_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__vec_push_int expects exactly 2 arguments") ==
                    std::string::npos)
            {
                fail("expected __vec_push_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __vec_get_int(); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_get_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_get_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_get_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__vec_get_int expects exactly 2 arguments") ==
                    std::string::npos)
            {
                fail("expected __vec_get_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { return __vec_get_int(nope, 0); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_get_int first-arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_get_int first-arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_get_int first-arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_get_int first argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Int { let v: Vec = __vec_new_int(2); return __vec_get_int(v, nope); }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_get_int second-arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_get_int second-arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_get_int second-arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_get_int second argument");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { __vec_set_int(); return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_set_int arity case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_set_int arity case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_set_int arity case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() ||
                diags[0].message.find("__vec_set_int expects exactly 3 arguments") ==
                    std::string::npos)
            {
                fail("expected __vec_set_int arity diagnostic");
            }
        }

        {
            const std::string source =
                "fn main() -> Unit { __vec_set_int(nope, 0, 1); return; }";
            const auto lexed = curlee::lexer::lex(source);
            if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
            {
                fail("expected lexing to succeed for __vec_set_int first-arg error case");
            }
            const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
            const auto parsed = curlee::parser::parse(tokens);
            if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
            {
                fail("expected parsing to succeed for __vec_set_int first-arg error case");
            }
            const auto& program = std::get<curlee::parser::Program>(parsed);
            const auto emitted = curlee::compiler::emit_bytecode(program);
            if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(emitted))
            {
                fail("expected emission to fail for __vec_set_int first-arg error case");
            }
            const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(emitted);
            if (diags.empty() || diags[0].message.find("unknown name 'nope'") == std::string::npos)
            {
                fail("expected unknown-name diagnostic for __vec_set_int first argument");
            }
        }
    }

    {
        const std::string source =
            "fn main() -> Int { let v: Vec = __vec_new_int(3); __vec_push_int(v, 7); "
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
            fail("expected __vec_* program to run and produce correct Int result");
        }
    }

    {
        const std::string source =
            "fn main() -> Int { let v: Vec = __vec_new_bool(4); __vec_push_bool(v, true); "
            "__vec_set_bool(v, 0, false); let s: Set = __set_new_int(); __set_insert_int(s, "
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
        using curlee::parser::MemberExpr;
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

    std::cout << "OK\n";
    return 0;
}
