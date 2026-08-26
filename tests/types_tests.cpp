// SPDX-License-Identifier: MIT
#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/ast.h>
#include <curlee/parser/parser.h>
#include <curlee/types/type.h>
#include <curlee/types/type_check.h>
#include <iostream>
#include <string_view>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static curlee::parser::Program parse_program_or_fail(std::string_view source,
                                                     const std::string& what)
{
    const auto lexed = curlee::lexer::lex(source);
    if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
    {
        fail("expected lexing to succeed for " + what);
    }
    const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
    auto parsed = curlee::parser::parse(tokens);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
    {
        fail("expected parsing to succeed for " + what);
    }
    return std::get<curlee::parser::Program>(std::move(parsed));
}

static std::vector<curlee::diag::Diagnostic> type_check_should_fail(std::string_view source,
                                                                    const std::string& what)
{
    const auto program = parse_program_or_fail(source, what);
    const auto typed = curlee::types::type_check(program);
    if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        fail("expected type checking to fail for " + what);
    }
    return std::get<std::vector<curlee::diag::Diagnostic>>(typed);
}

static curlee::types::TypeInfo type_check_should_succeed(std::string_view source,
                                                         const std::string& what)
{
    const auto program = parse_program_or_fail(source, what);
    const auto typed = curlee::types::type_check(program);
    if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
    {
        fail("expected type checking to succeed for " + what);
    }
    return std::get<curlee::types::TypeInfo>(typed);
}

static bool has_diag_containing(const std::vector<curlee::diag::Diagnostic>& diags,
                                const std::string& needle)
{
    for (const auto& d : diags)
    {
        if (d.message.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

int main()
{
    using namespace curlee::types;

    if (!(to_string(TypeKind::Struct) == "Struct" && to_string(TypeKind::Enum) == "Enum" &&
            to_string(TypeKind::Vec) == "Vec" && to_string(TypeKind::Set) == "Set" &&
          to_string(Type{.kind = TypeKind::Struct, .name = "S"}) == "S" &&
            to_string(Type{.kind = TypeKind::Enum, .name = "E"}) == "E" &&
            to_string(Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Int}) == "Vec" &&
            to_string(Type{.kind = TypeKind::Set, .element_kind = TypeKind::Int}) == "Set"))
    {
        fail("expected TypeKind and nominal Type stringification to work");
    }

    if (!(!(Type{.kind = TypeKind::Int} == Type{.kind = TypeKind::Bool}) &&
          (Type{.kind = TypeKind::Struct, .name = "S"} ==
           Type{.kind = TypeKind::Struct, .name = "S"}) &&
          (Type{.kind = TypeKind::Enum, .name = "E"} ==
           Type{.kind = TypeKind::Enum, .name = "E"}) &&
          !(Type{.kind = TypeKind::Struct, .name = "S"} ==
            Type{.kind = TypeKind::Struct, .name = "T"}) &&
          !(Type{.kind = TypeKind::Enum, .name = "E"} ==
            Type{.kind = TypeKind::Enum, .name = "F"}) &&
          !(Type{.kind = TypeKind::Struct, .name = "S"} ==
                        Type{.kind = TypeKind::Enum, .name = "S"}) &&
                    (Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Int} ==
                     Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Int}) &&
                    !(Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Int} ==
                        Type{.kind = TypeKind::Vec, .element_kind = TypeKind::Bool}) &&
                    (Type{.kind = TypeKind::Set, .element_kind = TypeKind::Int} ==
                     Type{.kind = TypeKind::Set, .element_kind = TypeKind::Int}) &&
                    !(Type{.kind = TypeKind::Set, .element_kind = TypeKind::Int} ==
                      Type{.kind = TypeKind::Set, .element_kind = TypeKind::Bool}) &&
                    (Type{.kind = TypeKind::Vec,
                         .element_kind = TypeKind::Struct,
                         .element_name = "Point"} ==
                     Type{.kind = TypeKind::Vec,
                         .element_kind = TypeKind::Struct,
                         .element_name = "Point"}) &&
                    !(Type{.kind = TypeKind::Vec,
                          .element_kind = TypeKind::Struct,
                          .element_name = "Point"} ==
                      Type{.kind = TypeKind::Vec,
                          .element_kind = TypeKind::Struct,
                          .element_name = "Other"}) &&
                    !(Type{.kind = TypeKind::Set,
                          .element_kind = TypeKind::Enum,
                          .element_name = "E"} ==
                      Type{.kind = TypeKind::Set,
                          .element_kind = TypeKind::Enum,
                          .element_name = "F"})))
    {
        fail("expected Type equality to respect kind and nominal name");
    }

    if (core_type_from_name("Int") != Type{.kind = TypeKind::Int})
    {
        fail("expected Int core type");
    }
    if (core_type_from_name("Bool") != Type{.kind = TypeKind::Bool})
    {
        fail("expected Bool core type");
    }
    if (core_type_from_name("String") != Type{.kind = TypeKind::String})
    {
        fail("expected String core type");
    }
    if (core_type_from_name("Unit") != Type{.kind = TypeKind::Unit})
    {
        fail("expected Unit core type");
    }
    if (core_type_from_name("Nope").has_value())
    {
        fail("expected unknown type name to map to nullopt");
    }

    {
        const FunctionType ft{
            .params = {Type{.kind = TypeKind::Int}, Type{.kind = TypeKind::Bool}},
            .result = Type{.kind = TypeKind::Unit},
        };

        const FunctionType same{
            .params = {Type{.kind = TypeKind::Int}, Type{.kind = TypeKind::Bool}},
            .result = Type{.kind = TypeKind::Unit},
        };

        const FunctionType different{
            .params = {Type{.kind = TypeKind::Int}},
            .result = Type{.kind = TypeKind::Unit},
        };

        const FunctionType different_result{
            .params = {Type{.kind = TypeKind::Int}, Type{.kind = TypeKind::Bool}},
            .result = Type{.kind = TypeKind::Int},
        };

        if (!(ft == same))
        {
            fail("expected function type equality to hold");
        }
        if (ft == different)
        {
            fail("expected different function types to compare unequal");
        }
        if (ft == different_result)
        {
            fail("expected different function result types to compare unequal");
        }
    }

    {
        const CapabilityType a{.name = "std.fs"};
        const CapabilityType b{.name = "std.fs"};
        const CapabilityType c{.name = "std.net"};

        if (!(a == b))
        {
            fail("expected capability types with the same name to compare equal");
        }
        if (a == c)
        {
            fail("expected capability types with different names to compare unequal");
        }
    }

    {
        const std::string source = "fn main() -> Int { let x: Int = 1; return x + 1; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for type info test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for type info test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to succeed for type info test");
        }

        const auto& info = std::get<curlee::types::TypeInfo>(typed);
        const auto& main_fn = program.functions.at(0);
        const auto& let_stmt = std::get<curlee::parser::LetStmt>(main_fn.body.stmts.at(0).node);
        const auto& ret_stmt = std::get<curlee::parser::ReturnStmt>(main_fn.body.stmts.at(1).node);

        if (!ret_stmt.value.has_value())
        {
            fail("expected return expression in type info test");
        }

        const auto let_it = info.expr_types.find(let_stmt.value.id);
        if (let_it == info.expr_types.end() || let_it->second.kind != TypeKind::Int)
        {
            fail("expected type info for let initializer");
        }

        const auto& return_expr = *ret_stmt.value;
        const auto ret_it = info.expr_types.find(return_expr.id);
        if (ret_it == info.expr_types.end() || ret_it->second.kind != TypeKind::Int)
        {
            fail("expected type info for return expression");
        }

        const auto* ret_bin = std::get_if<curlee::parser::BinaryExpr>(&return_expr.node);
        if (ret_bin == nullptr)
        {
            fail("expected return expression to be a BinaryExpr");
        }

        const auto ret_lhs_t = info.type_of(ret_bin->lhs->id);
        if (!ret_lhs_t.has_value() || ret_lhs_t->kind != TypeKind::Int)
        {
            fail("expected type info for return lhs");
        }

        const auto ret_rhs_t = info.type_of(ret_bin->rhs->id);
        if (!ret_rhs_t.has_value() || ret_rhs_t->kind != TypeKind::Int)
        {
            fail("expected type info for return rhs");
        }

        const auto span_text = source.substr(return_expr.span.start, return_expr.span.length());
        if (span_text != "x + 1")
        {
            fail("expected return expression span to match source text");
        }
    }

    {
        const std::string source = "fn main() -> Bool { let b: Bool = true; return b; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for bool literal type test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for bool literal type test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to succeed for bool literal type test");
        }

        const auto& info = std::get<curlee::types::TypeInfo>(typed);
        const auto& main_fn = program.functions.at(0);
        const auto& let_stmt = std::get<curlee::parser::LetStmt>(main_fn.body.stmts.at(0).node);
        const auto let_it = info.expr_types.find(let_stmt.value.id);
        if (let_it == info.expr_types.end() || let_it->second.kind != TypeKind::Bool)
        {
            fail("expected Bool type info for bool literal initializer");
        }
    }

    {
        // Span precision through typing: condition error should point at exactly `1`.
        const std::string source = "fn main() -> Int { if (1) { return 0; } return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for span precision test");
        }

        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for span precision test");
        }

        const auto& program = std::get<curlee::parser::Program>(parsed);
        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for span precision test");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        if (diags.empty())
        {
            fail("expected at least one diagnostic");
        }

        const auto& d = diags.front();
        if (!d.span.has_value())
        {
            fail("expected diagnostic to include a span");
        }
        const std::string_view span_text =
            std::string_view(source).substr(d.span->start, d.span->length());
        if (span_text != "1")
        {
            fail("expected condition diagnostic span to cover exactly `1`");
        }
        if (d.message.find("if condition type mismatch") == std::string::npos)
        {
            fail("expected if condition type mismatch diagnostic message");
        }
    }

    {
        // Assignment (issue #268): reassigning an existing local binding with
        // a value of a different type is a type error.
        const std::string source =
            "fn main() -> Int { let y: Int = 3; y = true; return y; }";
        const auto diags = type_check_should_fail(source, "assignment type mismatch");
        if (!has_diag_containing(diags, "type mismatch in assignment"))
        {
            fail("expected assignment type mismatch diagnostic");
        }
    }

    {
        // Assignment: parameters are read-only bindings; reassigning one is a
        // type-checker error (not a silent no-op).
        const std::string source = "fn f(n: Int) -> Int { n = 2; return n; } "
                                   "fn main() -> Int { return f(0); }";
        const auto diags = type_check_should_fail(source, "assignment to parameter");
        if (!has_diag_containing(diags, "cannot assign to 'n'"))
        {
            fail("expected read-only parameter assignment diagnostic");
        }
    }

    {
        // Assignment: ghost snapshots are read-only too.
        const std::string source =
            "fn main() -> Int { ghost let g: Int = 0; g = 1; return g; }";
        const auto diags = type_check_should_fail(source, "assignment to ghost snapshot");
        if (!has_diag_containing(diags, "cannot assign to 'g'"))
        {
            fail("expected read-only ghost snapshot assignment diagnostic");
        }
    }

    {
        // Assignment: same-type reassignment of a local binding type-checks
        // (Int and Bool both work).
        (void)type_check_should_succeed(
            "fn main() -> Int { let y: Int = 0; y = y + 1; return y; }",
            "int assignment type checks");
        (void)type_check_should_succeed(
            "fn main() -> Bool { let b: Bool = true; b = false; return b; }",
            "bool assignment type checks");
    }

    {
        // Fixed-size arrays (issue #278): `let q: [T; N] = [v; N];` with
        // indexed element reads/writes type-checks (acceptance #1).
        (void)type_check_should_succeed(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; q[0] = 42; "
            "let v: Int = q[0]; return v; }",
            "array let + indexed read/write type checks");
        // The Int-LITERAL -> unsigned element adaptation on the repeat
        // initializer (`[0; 16]` into a [U8; 16] binding) and on element
        // writes (`rx[i] = 2`) mirrors the port_outb rule (acceptance #4).
        (void)type_check_should_succeed(
            "fn main() -> Int { let rx: [U8; 16] = [0; 16]; rx[0] = 2; "
            "let s: U8 = rx[0]; return s + 0; }",
            "U8 array with Int-literal adaptation type checks");
        // U64 elements are supported; reads produce the U64 element type.
        (void)type_check_should_succeed(
            "fn main() -> Int { let u: [U64; 4] = [0; 4]; u[0] = 5; "
            "let v: U64 = u[0]; return 0; }",
            "U64 array element read type checks");
    }

    {
        // Arrays: a constant out-of-bounds index is rejected on both reads and
        // writes (acceptance #2: no silent OOB).
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; q[8] = 7; let v: Int = q[8]; "
            "return v; }",
            "constant OOB array index");
        if (!has_diag_containing(diags, "array index out of bounds"))
        {
            fail("expected constant OOB index diagnostic");
        }
    }

    {
        // Arrays: a constant negative index is always out of bounds.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; let v: Int = q[-1]; return v; }",
            "constant negative array index");
        if (!has_diag_containing(diags, "array index out of bounds: constant index -1"))
        {
            fail("expected constant negative index diagnostic");
        }
    }

    {
        // Arrays: the initializer length must match the declared length.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 4]; return q[0]; }",
            "array length mismatch");
        if (!has_diag_containing(diags, "array type mismatch in let: expected [Int; 8], got [Int; 4]"))
        {
            fail("expected array length mismatch diagnostic");
        }
    }

    {
        // Arrays: an element write must match the element type (no implicit
        // Bool -> Int coercion).
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; q[0] = true; return q[0]; }",
            "array element write mismatch");
        if (!has_diag_containing(diags, "array element type mismatch in assignment"))
        {
            fail("expected array element write mismatch diagnostic");
        }
    }

    {
        // Arrays: a bare array name is not a first-class value; it must be
        // indexed to produce an element.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; let v: Int = q; return v; }",
            "bare array name as value");
        if (!has_diag_containing(diags, "must be indexed"))
        {
            fail("expected bare-array-value diagnostic");
        }
    }

    {
        // Arrays: multi-dimensional indexing is out of the MVP scope.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; let v: Int = q[0][1]; return v; }",
            "multi-dimensional array index");
        if (!has_diag_containing(diags, "multi-dimensional arrays are not supported"))
        {
            fail("expected multi-dimensional index diagnostic");
        }
    }

    {
        // Arrays: only an array binding can be indexed.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let x: Int = 3; let v: Int = x[0]; return v; }",
            "indexing a non-array value");
        if (!has_diag_containing(diags, "cannot index non-array value"))
        {
            fail("expected non-array index diagnostic");
        }
    }

    {
        // Arrays: struct fields, parameters, return types and ghost snapshots
        // are all rejected (freestanding-local `let` bindings only in the MVP).
        const auto field_diags = type_check_should_fail(
            "struct S { q: [Int; 8]; } fn main() -> Int { return 0; }",
            "array struct field");
        if (!has_diag_containing(field_diags, "not as struct fields"))
        {
            fail("expected array struct-field diagnostic");
        }
        const auto param_diags = type_check_should_fail(
            "fn f(q: [Int; 8]) -> Int { return 0; } fn main() -> Int { return 0; }",
            "array parameter");
        if (!has_diag_containing(param_diags, "not as parameters"))
        {
            fail("expected array parameter diagnostic");
        }
        const auto return_diags = type_check_should_fail(
            "fn f() -> [Int; 8] { let q: [Int; 8] = [0; 8]; return q; } "
            "fn main() -> Int { return 0; }",
            "array return type");
        if (!has_diag_containing(return_diags, "not as return types"))
        {
            fail("expected array return-type diagnostic");
        }
        const auto ghost_diags = type_check_should_fail(
            "fn main() -> Int { let q: [Int; 8] = [0; 8]; ghost let snap: [Int; 8] = q; "
            "return 0; }",
            "array ghost snapshot");
        if (!has_diag_containing(ghost_diags, "not as ghost snapshots"))
        {
            fail("expected array ghost-snapshot diagnostic");
        }
    }

    {
        // Arrays: unsupported element kinds (Bool is not a storable core
        // element type in the MVP) are rejected with a precise diagnostic.
        const auto diags = type_check_should_fail(
            "fn main() -> Int { let q: [Bool; 8] = [true; 8]; return 0; }",
            "unsupported array element kind");
        if (!has_diag_containing(diags, "array element type 'Bool' is not supported"))
        {
            fail("expected unsupported array element kind diagnostic");
        }
    }

    {
        // Struct literal: extra/unknown field should be rejected with a precise span.
        const std::string source = "struct Point { x: Int; y: Int; } fn main() -> Int { let p: "
                                   "Point = Point{ x: 1, y: 2, z: 3 }; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for extra struct field test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for extra struct field test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for extra struct field test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        bool saw_unknown_field = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown field") != std::string::npos && d.span.has_value())
            {
                const auto span_text =
                    source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
                if (span_text.find("z: 3") != std::string::npos)
                {
                    saw_unknown_field = true;
                }
            }
        }
        if (!saw_unknown_field)
        {
            fail("expected unknown field diagnostic with span covering `z: 3`");
        }
    }

    {
        // Struct literal: missing required field should be rejected.
        const std::string source = "struct Point { x: Int; y: Int; } fn main() -> Int { let p: "
                                   "Point = Point{ x: 1 }; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for missing struct field test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for missing struct field test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for missing struct field test");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        bool saw_missing_field = false;
        for (const auto& d : diags)
        {
            if (d.message.find("missing") != std::string::npos &&
                d.message.find("field") != std::string::npos)
            {
                if (!d.span.has_value())
                {
                    continue;
                }
                const auto span_text =
                    source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
                if (span_text.find("Point{") != std::string::npos)
                {
                    saw_missing_field = true;
                }
            }
        }
        if (!saw_missing_field)
        {
            fail("expected missing field diagnostic for struct literal");
        }
    }

    {
        // Struct literal: wrong field type should be rejected.
        const std::string source = "struct Point { x: Int; y: Int; } fn main() -> Int { let p: "
                                   "Point = Point{ x: true, y: 2 }; return 0; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for wrong struct field type test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for wrong struct field type test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for wrong struct field type test");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        bool saw_field_type = false;
        for (const auto& d : diags)
        {
            if (d.message.find("field") != std::string::npos &&
                d.message.find("type") != std::string::npos)
            {
                if (!d.span.has_value())
                {
                    continue;
                }
                const auto span_text =
                    source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
                if (span_text.find("x: true") != std::string::npos)
                {
                    saw_field_type = true;
                }
            }
        }
        if (!saw_field_type)
        {
            fail("expected field type mismatch diagnostic for struct literal");
        }
    }

    {
        // Field access: accessing a field on a non-struct should be rejected with a precise span.
        const std::string source = "fn main() -> Int { let x: Int = 1; return x.y; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for non-struct field access test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for non-struct field access test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for non-struct field access test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);

        bool saw = false;
        for (const auto& d : diags)
        {
            if (!d.span.has_value())
            {
                continue;
            }
            const auto span_text =
                source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
            if (span_text == "x.y" && d.message.find("non-struct") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected non-struct field access diagnostic on span `x.y`");
        }
    }

    {
        // Field access: unknown field on a struct should be rejected with a precise span.
        const std::string source =
            "struct Point { x: Int; y: Int; } fn main() -> Int { let p: Point = Point{ x: 1, y: "
            "2 }; return p.z; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for unknown struct field access test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for unknown struct field access test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for unknown struct field access test");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        bool saw = false;
        for (const auto& d : diags)
        {
            if (!d.span.has_value())
            {
                continue;
            }
            const auto span_text =
                source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
            if (span_text == "p.z" && d.message.find("unknown field") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unknown field access diagnostic on span `p.z`");
        }
    }

    {
        // Success case: correct struct literal + field access should type-check.
        const std::string source = "struct Point { x: Int; y: Int; } fn main() -> Int { let p: "
                                   "Point = Point{ y: 2, x: 1 }; return p.x; }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for struct success test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for struct success test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to succeed for struct success test");
        }

        const auto& info = std::get<curlee::types::TypeInfo>(typed);
        const auto& main_fn = program.functions.at(0);
        const auto& ret_stmt = std::get<curlee::parser::ReturnStmt>(main_fn.body.stmts.at(1).node);
        if (!ret_stmt.value.has_value())
        {
            fail("expected return expression in struct success test");
        }

        const auto ret_t = info.type_of(ret_stmt.value->id);
        if (!ret_t.has_value() || ret_t->kind != TypeKind::Int)
        {
            fail("expected p.x to have type Int");
        }
    }

    {
        // Enum constructor: payload type should be checked.
        const std::string source = "enum OptionInt { None; Some(Int); } fn main() -> OptionInt { "
                                   "return OptionInt::Some(true); }";

        const auto lexed = curlee::lexer::lex(source);
        if (std::holds_alternative<curlee::diag::Diagnostic>(lexed))
        {
            fail("expected lexing to succeed for enum payload type test");
        }
        const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(tokens);
        if (std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parsing to succeed for enum payload type test");
        }
        const auto& program = std::get<curlee::parser::Program>(parsed);

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for enum payload type test");
        }

        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("payload") != std::string::npos ||
                d.message.find("variant") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected enum constructor payload type diagnostic");
        }
    }

    {
        // Function signature: missing return type annotation should be rejected.
        const std::string source = "fn main() { return 0; }";

        const auto diags = type_check_should_fail(source, "missing return type annotation test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("missing return type annotation") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected missing return type annotation diagnostic");
        }
    }

    {
        // Unknown type names should be rejected with a precise span.
        const std::string source = "fn main() -> Int { let x: Nope = 0; return 0; }";

        const auto diags = type_check_should_fail(source, "unknown type test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown type") != std::string::npos && d.span.has_value())
            {
                const auto span_text =
                    source.substr(d.span->start, static_cast<std::size_t>(d.span->length()));
                if (span_text == "Nope")
                {
                    saw = true;
                }
            }
        }
        if (!saw)
        {
            fail("expected unknown type diagnostic on span `Nope`");
        }
    }

    {
        // Return without value in non-Unit function should be rejected.
        const std::string source = "fn main() -> Int { return; }";

        const auto diags = type_check_should_fail(source, "return; in non-Unit test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("return; used in non-Unit") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected return; used in non-Unit diagnostic");
        }
    }

    {
        // Unary operator typing: - expects Int.
        const std::string source = "fn main() -> Int { return -true; }";

        const auto diags = type_check_should_fail(source, "unary '-' type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unary '-' expects Int") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unary '-' expects Int diagnostic");
        }
    }

    {
        // Unary operator typing: ! expects Bool.
        const std::string source = "fn main() -> Bool { return !1; }";

        const auto diags = type_check_should_fail(source, "unary '!' type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unary '!' expects Bool") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unary '!' expects Bool diagnostic");
        }
    }

    {
        // Binary operator typing: '+' expects Int+Int or String+String.
        const std::string source = "fn main() -> Int { return \"a\" + 1; }";

        const auto diags = type_check_should_fail(source, "'+' operand type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("'+' expects Int+Int or String+String") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected '+' expects Int+Int or String+String diagnostic");
        }
    }

    {
        // Bitwise binary operator typing (issue #270): '&' rejects Bool.
        const std::string source = "fn main() -> Int { return true & 1; }";

        const auto diags = type_check_should_fail(source, "bitwise '&' type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("bitwise operators expect Int or unsigned integer") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected bitwise '&' type diagnostic");
        }
    }

    {
        // Bitwise binary operator typing (issue #270): same-typed Int operands
        // type check cleanly; '1 | 2' must succeed.
        const std::string source = "fn main() -> Int { return 1 | 2; }";
        (void)type_check_should_succeed(source, "bitwise '|' typing test");
    }

    {
        // Bitwise binary operator typing (issue #270): rhs-side mismatch
        // ('1 & true') is also rejected by the operand-kind check.
        const std::string source = "fn main() -> Int { return 1 & true; }";

        const auto diags = type_check_should_fail(source, "bitwise rhs type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("bitwise operators expect Int or unsigned integer") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected bitwise rhs type diagnostic");
        }
    }

    {
        // Bitwise binary operator typing (issue #274): a U8 value bitwise-ANDed
        // with an Int LITERAL is accepted — the literal adapts to the unsigned
        // width (mirroring port_outb(0x3F8, 0x48)), so `let y: U8 = x & 1;`
        // type-checks.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let x: U8 = phys<U8>(0x4000).read();
    let y: U8 = x & 1;
    let z: U8 = x | 64;
    let g: U8 = x & (32);   // grouped Int literal also adapts
    let l: U8 = 32 & x;     // Int literal on the other side adapts too
    if ((x & 32) != 0) {
      return 0;
    }
    return y + z;
  }
})";
        (void)type_check_should_succeed(source, "bitwise U8-with-literal typing test");
    }

    {
        // Bitwise binary operator typing (issue #274): a non-literal Int operand
        // widens the U8/U16/U32 side to Int, so the result is Int (not U8).
        const std::string source = R"(fn main(pm: cap phys.mem, m: Int) -> Int {
  unsafe {
    let x: U8 = phys<U8>(0x4000).read();
    let y: Int = x & m;
    return y;
  }
})";
        (void)type_check_should_succeed(source, "bitwise U8-with-Int-var typing test");
    }

    {
        // Bitwise binary operator typing (issue #274): mixed unsigned widths
        // (U8 & U16) remain a hard error — no cross-width bitwise.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let x: U8 = phys<U8>(0x4000).read();
    let w: U16 = phys<U16>(0x4002).read();
    let y: U16 = w & x;
    return 0;
  }
})";

        const auto diags = type_check_should_fail(source, "bitwise mixed-width typing test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("bitwise operators expect matching operand types") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected bitwise matching-type diagnostic");
        }
    }

    {
        // Bitwise binary operator typing (issue #274): U64 does not widen — a
        // U64 value bitwise-mixed with an Int is rejected.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let x: U64 = phys<U64>(0x4000).read();
    let y: U64 = x & 1;
    return 0;
  }
})";

        const auto diags = type_check_should_fail(source, "bitwise U64-with-literal typing test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("bitwise operators expect matching operand types") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected bitwise U64 matching-type diagnostic");
        }
    }

    {
        // Bitwise unary operator typing (issue #270): '~' rejects Bool.
        const std::string source = "fn main() -> Int { return ~true; }";

        const auto diags = type_check_should_fail(source, "unary '~' type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unary '~' expects Int or an unsigned integer type") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unary '~' type diagnostic");
        }
    }

    {
        // Bitwise unary operator typing (issue #270): '~' on an unsigned element
        // kind (U32, from a Phys read) is accepted and keeps the type.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let x: U32 = phys<U32>(0x4000).read();
    let y: U32 = ~x;
    return 0;
  }
})";
        (void)type_check_should_succeed(source, "unary '~' on U32 typing test");
    }

    {
        // Modulo typing (issue #270): '%' expects Int operands.
        const std::string source = "fn main() -> Int { return true % 2; }";

        const auto diags = type_check_should_fail(source, "'%' type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("'%' expects Int operands") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected '%' type diagnostic");
        }
    }

    {
        // Modulo typing (issue #270): rhs-side mismatch is also rejected.
        const std::string source = "fn main() -> Int { return 1 % true; }";

        const auto diags = type_check_should_fail(source, "'%' rhs type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("'%' expects Int operands") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected '%' rhs type diagnostic");
        }
    }

    // ---- Unsigned widening / comparisons / bitwise (issue #274) ----

    {
        // Arithmetic widening: a U8 value + Int literal widens to Int, so the
        // result binds to an Int let (the acceptance-criterion shape
        // `let wide: Int = v + 0;`).
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let v: U8 = port_inb(0x3FD);
    let wide: Int = v + 0;
    return wide;
  }
})";
        (void)type_check_should_succeed(source, "U8 arithmetic widening typing test");
    }

    {
        // Arithmetic widening: unsigned + unsigned (same or mixed widths) is
        // defined as Int arithmetic (no wraparound), so U8+U8 -> Int and
        // U16+U8 -> Int.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let a: U8 = port_inb(0x3FD);
    let b: U8 = port_inb(0x3FE);
    let w: U16 = port_inw(0x1CF);
    let s1: Int = a + b;
    let s2: Int = w + a;
    let d: Int = a - 1;
    let q: Int = a / 4;
    return s1 + s2 + d + q;
  }
})";
        (void)type_check_should_succeed(source, "unsigned arithmetic widening typing test");
    }

    {
        // U64 has no arithmetic/widening model: `u64 + 1` is a hard error even
        // with an Int literal (out of scope for issue #274).
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let x: U64 = phys<U64>(0x4000).read();
    let y: Int = x + 1;
    return 0;
  }
})";

        const auto diags = type_check_should_fail(source, "U64 arithmetic typing test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("arithmetic with U64 is not supported") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected U64 arithmetic diagnostic");
        }
    }

    {
        // Comparisons on unsigned values: U8 == U8, U16 vs an Int literal, and
        // U8 vs an Int variable all produce Bool (widening).
        const std::string source = R"(fn main(pm: cap phys.mem, lim: Int) -> Bool {
  unsafe {
    let m: U8 = port_inb(0x3FD);
    let v: U8 = port_inb(0x3FE);
    let xres: U16 = port_inw(0x1CF);
    if (m == v) {
      return xres != 640;
    } else {
      return v <= lim;
    }
  }
})";
        (void)type_check_should_succeed(source, "unsigned comparison typing test");
    }

    {
        // Mixed unsigned widths compare via widening (U8 < U16 -> Bool).
        const std::string source = R"(fn main(pm: cap phys.mem) -> Bool {
  unsafe {
    let a: U8 = port_inb(0x3FD);
    let w: U16 = port_inw(0x1CF);
    return a < w;
  }
})";
        (void)type_check_should_succeed(source, "mixed-width comparison typing test");
    }

    {
        // U64 does not widen: a U64-vs-Int comparison is a hard error.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Bool {
  unsafe {
    let x: U64 = phys<U64>(0x4000).read();
    return x == 5;
  }
})";

        const auto diags = type_check_should_fail(source, "U64 comparison typing test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("U64 does not widen") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected U64 comparison diagnostic");
        }
    }

    {
        // The putc_driver busy-wait shape type-checks end-to-end on a U8 port
        // read: bit-test with an Int literal, compare, and use as an `if`
        // condition.
        const std::string source = R"(fn main(pm: cap phys.mem) -> Int {
  unsafe {
    let lsr: U8 = port_inb(0x3FD);
    if ((lsr & 32) != 0) {
      port_outb(0x3F8, 0x48);
      return 0;
    }
    return 1;
  }
})";
        (void)type_check_should_succeed(source, "putc busy-wait typing test");
    }

    {
        // Builtins: user programs cannot declare builtin function 'print'.
        const std::string source = "fn print(x: Int) -> Unit { } fn main() -> Unit { }";

        const auto diags = type_check_should_fail(source, "declaring builtin print should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function 'print'") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function 'print' diagnostic");
        }
    }

    {
        // Calls: print arity and argument type should be validated.
        const std::string source_arity = "fn main() -> Unit { print(); }";
        {
            const auto diags = type_check_should_fail(source_arity, "print arity test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("print expects exactly 1 argument") != std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected print arity diagnostic");
            }
        }

        const std::string source_type =
            "struct Point { x: Int; } fn main() -> Unit { let p: Point = Point{ x: 1 }; print(p); "
            "}";
        {
            const auto diags = type_check_should_fail(source_type, "print argument type test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("print only supports Int, Bool, or String") != std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected print supported-types diagnostic");
            }
        }
    }

    {
        // AST robustness: exercise MemberExpr base==nullptr paths.
        // This covers both:
        // - collect_member_chain(MemberExpr{base=null}) returning false (call callee qualification)
        // - check_expr_node(MemberExpr{base=null}) producing "invalid member access".
        using curlee::parser::Block;
        using curlee::parser::CallExpr;
        using curlee::parser::Expr;
        using curlee::parser::ExprStmt;
        using curlee::parser::Function;
        using curlee::parser::MemberExpr;
        using curlee::parser::Program;
        using curlee::parser::ReturnStmt;
        using curlee::parser::Stmt;
        using curlee::parser::TypeName;
        using curlee::source::Span;

        Program program;

        Function main_fn;
        main_fn.span = Span{.start = 0, .end = 0};
        main_fn.name = "main";
        main_fn.return_type = TypeName{.span = Span{.start = 0, .end = 0}, .name = "Int"};
        main_fn.body = Block{.span = Span{.start = 0, .end = 0}, .stmts = {}};

        // Statement 1: call with invalid member callee (MemberExpr base == nullptr).
        Expr callee;
        callee.id = 1;
        callee.span = Span{.start = 0, .end = 0};
        callee.node = MemberExpr{.base = nullptr, .member = "f"};

        Expr call;
        call.id = 2;
        call.span = Span{.start = 0, .end = 0};
        call.node = CallExpr{.callee = std::make_unique<Expr>(std::move(callee)), .args = {}};

        Stmt stmt1;
        stmt1.span = Span{.start = 0, .end = 0};
        stmt1.node = ExprStmt{.expr = std::move(call)};

        // Statement 2: return invalid member expression directly.
        Expr bad_member;
        bad_member.id = 3;
        bad_member.span = Span{.start = 0, .end = 0};
        bad_member.node = MemberExpr{.base = nullptr, .member = "x"};

        ReturnStmt ret;
        ret.value = std::move(bad_member);

        Stmt stmt2;
        stmt2.span = Span{.start = 0, .end = 0};
        stmt2.node = std::move(ret);

        main_fn.body.stmts.push_back(std::move(stmt1));
        main_fn.body.stmts.push_back(std::move(stmt2));
        program.functions.push_back(std::move(main_fn));

        const auto typed = curlee::types::type_check(program);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed))
        {
            fail("expected type checking to fail for invalid MemberExpr AST test");
        }
        const auto& diags = std::get<std::vector<curlee::diag::Diagnostic>>(typed);

        bool saw_only_direct_calls = false;
        bool saw_invalid_member_access = false;
        for (const auto& d : diags)
        {
            if (d.message.find("only direct calls are supported") != std::string::npos)
            {
                saw_only_direct_calls = true;
            }
            if (d.message.find("invalid member access") != std::string::npos)
            {
                saw_invalid_member_access = true;
            }
        }

        if (!saw_only_direct_calls)
        {
            fail("expected only-direct-calls diagnostic for invalid member callee");
        }
        if (!saw_invalid_member_access)
        {
            fail("expected invalid member access diagnostic for MemberExpr base==nullptr");
        }
    }

    {
        // python_ffi.call requires unsafe, and is stubbed to 0 arguments.
        const std::string source_requires_unsafe = "fn main() -> Unit { python_ffi.call(); }";
        {
            const auto diags =
                type_check_should_fail(source_requires_unsafe, "python_ffi unsafe test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("python_ffi.call requires an unsafe context") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected python_ffi.call requires unsafe diagnostic");
            }
        }

        const std::string source_args = "fn main() -> Unit { unsafe { python_ffi.call(1); } }";
        {
            const auto diags = type_check_should_fail(source_args, "python_ffi args test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("python_ffi.call is stubbed") != std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected python_ffi.call stubbed-args diagnostic");
            }
        }
    }

    {
        // Enum variants that require payloads should reject bare references.
        const std::string source =
            "enum OptionInt { None; Some(Int); } fn main() -> OptionInt { return OptionInt::Some; "
            "}";

        const auto diags = type_check_should_fail(source, "enum variant requires payload test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("requires a payload") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected enum variant requires payload diagnostic");
        }
    }

    {
        // Enum constructor calls should validate payload arity.
        const std::string source =
            "enum OptionInt { Some(Int); } fn main() -> OptionInt { return OptionInt::Some(); }";

        const auto diags = type_check_should_fail(source, "enum payload arity test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("expects exactly 1 payload argument") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected enum payload arity diagnostic");
        }
    }

    {
        // Calls: module-qualified calls should validate import qualifiers.
        const std::string source_unknown_qual =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return nope.f(1); }";

        const auto diags = type_check_should_fail(source_unknown_qual, "unknown module qualifier");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown module qualifier in call") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unknown module qualifier diagnostic");
        }

        const std::string source_ok =
            "import foo; fn f(x: Int) -> Int { return x; } fn main() -> Int { return foo.f(1); }";
        (void)type_check_should_succeed(source_ok, "module-qualified call with import test");
    }

    {
        const std::string source =
            "struct T { x: Int; } enum T { A; } fn main() -> Int { return 0; }";
        const auto diags = type_check_should_fail(source, "duplicate type name struct/enum test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("duplicate type name") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected duplicate type name diagnostic");
        }
    }

    {
        const std::string source = "fn main() -> Int { let x: Bool = 1; return 0; }";
        const auto diags = type_check_should_fail(source, "let type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("type mismatch in let") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected let type mismatch diagnostic");
        }
    }

    {
        const std::string source = "fn main() -> Int { return true; }";
        const auto diags = type_check_should_fail(source, "return type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("return type mismatch") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected return type mismatch diagnostic");
        }
    }

    {
        const std::string source = "fn main() -> Int { if (1) { return 1; } return 0; }";
        const auto diags = type_check_should_fail(source, "if condition type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("if condition type mismatch") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected if condition type mismatch diagnostic");
        }
    }

    {
        const std::string source = "fn main() -> Int { while (1) { return 1; } return 0; }";
        const auto diags = type_check_should_fail(source, "while condition type mismatch test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("while condition type mismatch") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected while condition type mismatch diagnostic");
        }
    }

    {
        const std::string source =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { let y: Int = f; return 0; }";
        const auto diags = type_check_should_fail(source, "function name not a value test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("is not a value") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected function name is not a value diagnostic");
        }
    }

    {
        const std::string source_unknown_field =
            "struct Point { x: Int; } fn main() -> Point { return Point{ x: 1, y: 2 }; }";
        const auto diags =
            type_check_should_fail(source_unknown_field, "struct literal unknown field test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown field 'y'") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected struct literal unknown field diagnostic");
        }

        const std::string source_missing =
            "struct Point { x: Int; y: Int; } fn main() -> Point { return Point{ x: 1 }; }";
        {
            const auto diags =
                type_check_should_fail(source_missing, "struct literal missing field test");
            bool saw_missing = false;
            for (const auto& d : diags)
            {
                if (d.message.find("is missing required field") != std::string::npos)
                {
                    saw_missing = true;
                }
            }
            if (!saw_missing)
            {
                fail("expected struct literal missing required field diagnostic");
            }
        }

        const std::string source_mismatch =
            "struct Point { x: Int; } fn main() -> Point { return Point{ x: true }; }";
        {
            const auto diags =
                type_check_should_fail(source_mismatch, "struct literal field type mismatch test");
            bool saw_mismatch = false;
            for (const auto& d : diags)
            {
                if (d.message.find("type mismatch") != std::string::npos)
                {
                    saw_mismatch = true;
                }
            }
            if (!saw_mismatch)
            {
                fail("expected struct literal field type mismatch diagnostic");
            }
        }
    }

    {
        const std::string source_non_struct = "fn main() -> Int { return 1.x; }";
        const auto diags = type_check_should_fail(source_non_struct, "member access non-struct");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot access field") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected member access non-struct diagnostic");
        }

        const std::string source_unknown_field = "struct Point { x: Int; } fn main() -> Int { let "
                                                 "p: Point = Point{ x: 1 }; return p.y; }";
        {
            const auto diags = type_check_should_fail(source_unknown_field,
                                                      "member access unknown field on struct");
            bool saw_unknown = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown field 'y'") != std::string::npos)
                {
                    saw_unknown = true;
                }
            }
            if (!saw_unknown)
            {
                fail("expected unknown field on struct diagnostic");
            }
        }
    }

    {
        const std::string source = "enum E { A; } fn main() -> E { return E::A(1); }";
        const auto diags = type_check_should_fail(source, "enum no-payload call with args");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("does not take a payload") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected enum no-payload diagnostic");
        }
    }

    {
        const std::string source_unknown_enum =
            "fn main() -> Int { let x: Int = 0; return Nope::A; }";
        const auto diags = type_check_should_fail(source_unknown_enum, "scoped name unknown enum");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown enum type") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unknown enum type diagnostic");
        }

        const std::string source_unknown_variant = "enum E { A; } fn main() -> E { return E::B; }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_variant, "scoped name unknown variant");
            bool saw_unknown = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown variant") != std::string::npos)
                {
                    saw_unknown = true;
                }
            }
            if (!saw_unknown)
            {
                fail("expected unknown enum variant diagnostic");
            }
        }
    }

    {
        const std::string source_unknown_fn = "fn main() -> Int { return foo(1); }";
        const auto diags = type_check_should_fail(source_unknown_fn, "unknown function call test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown function") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unknown function diagnostic");
        }

        const std::string source_arity =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return f(1, 2); }";
        {
            const auto diags = type_check_should_fail(source_arity, "user call arity mismatch");
            bool saw_arity = false;
            for (const auto& d : diags)
            {
                if (d.message.find("wrong number of arguments") != std::string::npos)
                {
                    saw_arity = true;
                }
            }
            if (!saw_arity)
            {
                fail("expected user call arity mismatch diagnostic");
            }
        }

        const std::string source_arg_type =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return f(true); }";
        {
            const auto diags =
                type_check_should_fail(source_arg_type, "user call arg type mismatch");
            bool saw_type = false;
            for (const auto& d : diags)
            {
                if (d.message.find("argument type mismatch") != std::string::npos)
                {
                    saw_type = true;
                }
            }
            if (!saw_type)
            {
                fail("expected user call argument type mismatch diagnostic");
            }
        }
    }

    {
        const std::string source =
            "fn f(x: Int) -> Int { return x; } fn main() -> Int { return (1 + 2).f(3); }";
        const auto diags = type_check_should_fail(source, "non-name callee call rejected test");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("only direct calls are supported") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected only direct calls are supported diagnostic");
        }
    }

    {
        const auto info = type_check_should_succeed("enum Option { Some(Int); None; }"
                                                    "fn main(v: Option) -> Int {"
                                                    "  match (v) {"
                                                    "    Option::Some(x) => { return x; }"
                                                    "    Option::None => { return 0; }"
                                                    "  }"
                                                    "}",
                                                    "match success with payload-bearing variants");
        if (info.expr_types.empty())
        {
            fail("expected type info to be collected for match success program");
        }
    }

    {
        const auto diags = type_check_should_fail("enum Option { Some(Int); None; }"
                                                  "fn main(v: Option) -> Int {"
                                                  "  match (v) {"
                                                  "    Option::Some(x) => { return x; }"
                                                  "  }"
                                                  "  return 0;"
                                                  "}",
                                                  "match missing-arm diagnostics");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("non-exhaustive match") != std::string::npos &&
                d.message.find("Option::None") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected non-exhaustive match diagnostic with missing variant list");
        }
    }

    {
        const auto diags = type_check_should_fail("enum E { A; B; }"
                                                  "fn main(v: E) -> Int {"
                                                  "  match (v) {"
                                                  "    E::A => { return 1; }"
                                                  "    E::A => { return 2; }"
                                                  "    E::B => { return 3; }"
                                                  "  }"
                                                  "}",
                                                  "match duplicate-arm diagnostics");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("duplicate match arm") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected duplicate match arm diagnostic");
        }
    }

    {
        const auto diags = type_check_should_fail("enum Option { Some(Int); None; }"
                                                  "fn main(v: Option) -> Int {"
                                                  "  match (v) {"
                                                  "    Option::Some => { return 0; }"
                                                  "    Option::None => { return 1; }"
                                                  "  }"
                                                  "}",
                                                  "match payload-binding required diagnostics");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("must bind payload") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected payload-binding required diagnostic in match arm");
        }
    }

    {
        const auto diags = type_check_should_fail("enum Option { Some(Int); None; }"
                                                  "fn main(v: Option) -> Int {"
                                                  "  match (v) {"
                                                  "    Option::Some(x) => { return x; }"
                                                  "    Option::None(z) => { return 0; }"
                                                  "  }"
                                                  "}",
                                                  "match payload-binding forbidden diagnostics");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot bind payload") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected payload-binding forbidden diagnostic for no-payload variant");
        }
    }

    // Phys<T>: TypeKind stringification and equality for the new kinds.
    {
        if (to_string(TypeKind::U8) != "U8" || to_string(TypeKind::U16) != "U16" ||
            to_string(TypeKind::U32) != "U32" || to_string(TypeKind::U64) != "U64" ||
            to_string(TypeKind::Phys) != "Phys")
        {
            fail("expected unsigned and Phys TypeKind stringification to work");
        }
        if (to_string(Type{.kind = TypeKind::Phys,
                          .name = "Phys",
                          .element_kind = TypeKind::U32,
                          .element_name = "U32"}) != "Phys")
        {
            fail("expected Phys Type stringification to return Phys");
        }

        const Type phys_u32{.kind = TypeKind::Phys,
                            .name = "Phys",
                            .element_kind = TypeKind::U32,
                            .element_name = "U32"};
        const Type phys_u32_same{.kind = TypeKind::Phys,
                                 .name = "Phys",
                                 .element_kind = TypeKind::U32,
                                 .element_name = "U32"};
        const Type phys_u8{.kind = TypeKind::Phys,
                           .name = "Phys",
                           .element_kind = TypeKind::U8,
                           .element_name = "U8"};
        if (!(phys_u32 == phys_u32_same) || (phys_u32 == phys_u8))
        {
            fail("expected Phys equality to respect element kind");
        }
    }

    // Fixed-size arrays (issue #278): TypeKind stringification, the display
    // string with the numeric length, array equality (length-sensitive) and
    // the element-kind gate.
    {
        if (to_string(TypeKind::Array) != "Array")
        {
            fail("expected Array TypeKind stringification to work");
        }
        const Type arr{.kind = TypeKind::Array,
                       .element_kind = TypeKind::Int,
                       .element_name = "Int",
                       .array_len = 8};
        if (to_display_string(arr) != "[Int; 8]")
        {
            fail("expected array display string '[Int; 8]'");
        }
        const Type arr_u8{.kind = TypeKind::Array,
                          .element_kind = TypeKind::U8,
                          .element_name = "U8",
                          .array_len = 16};
        if (to_display_string(arr_u8) != "[U8; 16]")
        {
            fail("expected array display string '[U8; 16]'");
        }
        // Equality respects the length: [Int; 8] != [Int; 4].
        const Type arr_short{.kind = TypeKind::Array,
                             .element_kind = TypeKind::Int,
                             .element_name = "Int",
                             .array_len = 4};
        if (arr == arr_short || !(arr == Type{.kind = TypeKind::Array,
                                              .element_kind = TypeKind::Int,
                                              .element_name = "Int",
                                              .array_len = 8}))
        {
            fail("expected array equality to respect the length");
        }
        if (!is_array_element_kind(TypeKind::Int) || !is_array_element_kind(TypeKind::U8) ||
            !is_array_element_kind(TypeKind::U16) || !is_array_element_kind(TypeKind::U32) ||
            !is_array_element_kind(TypeKind::U64) || is_array_element_kind(TypeKind::Bool))
        {
            fail("expected is_array_element_kind to be exact");
        }
    }

    // Phys<T>: core_type_from_name and element-kind helpers.
    {
        if (core_type_from_name("U8") != Type{.kind = TypeKind::U8})
        {
            fail("expected U8 core type");
        }
        if (core_type_from_name("U16") != Type{.kind = TypeKind::U16})
        {
            fail("expected U16 core type");
        }
        if (core_type_from_name("U32") != Type{.kind = TypeKind::U32})
        {
            fail("expected U32 core type");
        }
        if (core_type_from_name("U64") != Type{.kind = TypeKind::U64})
        {
            fail("expected U64 core type");
        }
        if (phys_element_kind_from_name("U8") != TypeKind::U8 ||
            phys_element_kind_from_name("U16") != TypeKind::U16 ||
            phys_element_kind_from_name("U32") != TypeKind::U32 ||
            phys_element_kind_from_name("U64") != TypeKind::U64 ||
            phys_element_kind_from_name("String").has_value())
        {
            fail("expected phys element-kind resolution to work");
        }
        if (!is_phys_element_kind(TypeKind::U8) || !is_phys_element_kind(TypeKind::U64) ||
            is_phys_element_kind(TypeKind::Int))
        {
            fail("expected is_phys_element_kind to be exact");
        }
    }

    // Phys<T>: positive fixture passes type checking (unsafe + cap phys.mem).
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    fb.write(0xFF8800);
    let v: U32 = fb.read();
  }
  return;
}
)";
        (void)type_check_should_succeed(source, "phys positive fixture");
    }

    // Phys<T>: all four supported element kinds type-check.
    {
        for (const auto* kind : {"U8", "U16", "U32", "U64"})
        {
            const std::string source = std::string(R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<)") + kind + R"(> = phys<)" + kind + R"(>(0x1000);
  }
  return;
}
)";
            (void)type_check_should_succeed(source, "phys element kind " + std::string(kind));
        }
    }

    // Phys<T>: read/write outside `unsafe` fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  let fb: Phys<U32> = phys<U32>(0xFD00_0000);
  let v: U32 = fb.read();
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys read outside unsafe");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("requires an unsafe block") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unsafe-block diagnostic for phys read");
        }
    }

    // Phys<T>: missing `cap phys.mem` parameter fails.
    {
        const std::string source = R"(fn main() -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    let v: U32 = fb.read();
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys missing cap");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("phys.mem") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected missing phys.mem capability diagnostic");
        }
    }

    // Phys<T>: non-literal addresses are rejected at parse time (parser requires a literal),
    // so the type-checker constant-literal rule is defense-in-depth only. Verify the parse
    // rejection surfaces a stable diagnostic.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(100 + 4);
  }
  return;
}
)";
        const auto lexed = curlee::lexer::lex(source);
        const auto& toks = std::get<std::vector<curlee::lexer::Token>>(lexed);
        const auto parsed = curlee::parser::parse(toks);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            fail("expected parse failure for phys non-literal address");
        }
        bool saw = false;
        for (const auto& d : std::get<std::vector<curlee::diag::Diagnostic>>(parsed))
        {
            if (d.message.find("phys() expects an integer literal address") != std::string::npos ||
                d.message.find("expected ')' after phys address") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected literal-address parse diagnostic for phys non-literal address");
        }
    }

    // Phys<T>: arithmetic on Phys<T> fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    let x: Int = fb + 1;
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys arithmetic");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("'+' expects") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected arithmetic-on-Phys diagnostic");
        }
    }

    // Phys<T>: unsupported element kind (Phys<String>) fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<String> = phys<String>(0xFD00_0000);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys string element");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unsupported Phys element kind") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unsupported Phys element kind diagnostic");
        }
    }

    // Phys<T>: `Phys` without a type argument fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys = phys<U32>(0xFD00_0000);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys missing type arg");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("Phys requires an element type argument") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected missing type-argument diagnostic for Phys");
        }
    }

    // Phys<T>: read() on a non-Phys value fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let x: Int = 5;
    let y: Int = x.read();
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys read on non-phys");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("read() can only be called on a Phys<T> value") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected read-on-non-Phys diagnostic");
        }
    }

    // Phys<T>: write() with a wrong (non-Int) value type fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    fb.write(true);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys write value type mismatch");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("write() value type mismatch") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected write value type mismatch diagnostic");
        }
    }

    // Phys<T>: write() on a non-Phys value fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let x: Int = 5;
    x.write(1);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys write on non-phys");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("write() can only be called on a Phys<T> value") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected write-on-non-Phys diagnostic");
        }
    }

    // Phys<T>: write() outside `unsafe` fails.
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  let fb: Phys<U32> = phys<U32>(0xFD00_0000);
  fb.write(1);
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys write outside unsafe");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("requires an unsafe block") != std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected unsafe-block diagnostic for phys write");
        }
    }

    // Phys<T>: read() on an unknown name fails type-check of the base expression
    // (exercises the `!base_t.has_value()` guard in the read handler).
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let v: U32 = missing.read();
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys read on unknown base");
        bool saw_unknown = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown name") != std::string::npos)
            {
                saw_unknown = true;
            }
        }
        if (!saw_unknown)
        {
            fail("expected unknown-name diagnostic for phys read base");
        }
    }

    // Phys<T>: write() with an unknown value name fails type-check of the value
    // (exercises the `!value_t.has_value()` guard in the write handler).
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    let fb: Phys<U32> = phys<U32>(0xFD00_0000);
    fb.write(missing);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys write unknown value");
        bool saw_unknown = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown name") != std::string::npos)
            {
                saw_unknown = true;
            }
        }
        if (!saw_unknown)
        {
            fail("expected unknown-name diagnostic for phys write value");
        }
    }

    // Phys<T>: write() on an unknown base name fails type-check of the base
    // (exercises the `!base_t.has_value()` guard in the write handler).
    {
        const std::string source = R"(fn main(pm: cap phys.mem) -> Unit {
  unsafe {
    missing.write(1);
  }
  return;
}
)";
        const auto diags = type_check_should_fail(source, "phys write unknown base");
        bool saw_unknown = false;
        for (const auto& d : diags)
        {
            if (d.message.find("unknown name") != std::string::npos)
            {
                saw_unknown = true;
            }
        }
        if (!saw_unknown)
        {
            fail("expected unknown-name diagnostic for phys write base");
        }
    }

    // Phys<T>: directly-constructed AST reaches the defensive literal-validation and
    // element-kind branches that the parser otherwise prevents (defense in depth).
    {
        // Bad element kind on the PhysExpr (parser accepts any identifier, so the
        // type checker is the final gate).
        curlee::parser::Program prog_bad_elem;
        {
            curlee::parser::Function f;
            f.name = "main";
            f.span = {};
            f.return_type = curlee::parser::TypeName{.span = {}, .name = "Unit"};

            // let fb: Phys<U32> = phys<Bad>(0x1000);
            curlee::parser::Expr phys_expr;
            phys_expr.span = {};
            phys_expr.node = curlee::parser::PhysExpr{.element_kind = "Bad",
                                                      .lexeme = "0x1000"};

            curlee::parser::TypeName fb_type;
            fb_type.span = {};
            fb_type.name = "Phys";
            fb_type.type_arg = "U32";

            curlee::parser::LetStmt let_stmt;
            let_stmt.name = "fb";
            let_stmt.type = fb_type;
            let_stmt.value = std::move(phys_expr);

            curlee::parser::Stmt s;
            s.span = {};
            s.node = std::move(let_stmt);
            f.body.stmts.push_back(std::move(s));

            curlee::parser::Stmt ret;
            ret.span = {};
            ret.node = curlee::parser::ReturnStmt{.value = std::nullopt};
            f.body.stmts.push_back(std::move(ret));

            prog_bad_elem.functions.push_back(std::move(f));
        }

        const auto typed_bad = curlee::types::type_check(prog_bad_elem);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed_bad))
        {
            fail("expected type check failure for bad Phys element kind");
        }
        bool saw_bad_kind = false;
        for (const auto& d : std::get<std::vector<curlee::diag::Diagnostic>>(typed_bad))
        {
            if (d.message.find("unsupported Phys element kind") != std::string::npos)
            {
                saw_bad_kind = true;
            }
        }
        if (!saw_bad_kind)
        {
            fail("expected unsupported Phys element kind diagnostic from direct AST");
        }

        // Bad (non-literal) address lexeme reaches the constant-literal guard.
        curlee::parser::Program prog_bad_addr;
        {
            curlee::parser::Function f;
            f.name = "main";
            f.span = {};
            f.return_type = curlee::parser::TypeName{.span = {}, .name = "Unit"};

            curlee::parser::Expr phys_expr;
            phys_expr.span = {};
            phys_expr.node = curlee::parser::PhysExpr{.element_kind = "U32",
                                                      .lexeme = "base + 4"};

            curlee::parser::TypeName fb_type;
            fb_type.span = {};
            fb_type.name = "Phys";
            fb_type.type_arg = "U32";

            curlee::parser::LetStmt let_stmt;
            let_stmt.name = "fb";
            let_stmt.type = fb_type;
            let_stmt.value = std::move(phys_expr);

            curlee::parser::Stmt s;
            s.span = {};
            s.node = std::move(let_stmt);
            f.body.stmts.push_back(std::move(s));

            curlee::parser::Stmt ret;
            ret.span = {};
            ret.node = curlee::parser::ReturnStmt{.value = std::nullopt};
            f.body.stmts.push_back(std::move(ret));

            prog_bad_addr.functions.push_back(std::move(f));
        }

        const auto typed_addr = curlee::types::type_check(prog_bad_addr);
        if (!std::holds_alternative<std::vector<curlee::diag::Diagnostic>>(typed_addr))
        {
            fail("expected type check failure for non-literal phys address");
        }
        bool saw_addr = false;
        for (const auto& d : std::get<std::vector<curlee::diag::Diagnostic>>(typed_addr))
        {
            if (d.message.find("physical address must be a constant literal") != std::string::npos)
            {
                saw_addr = true;
            }
        }
        if (!saw_addr)
        {
            fail("expected constant-literal diagnostic from direct AST");
        }
    }

    std::cout << "OK\n";
    return 0;
}
