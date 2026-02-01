#include <cstdlib>
#include <curlee/lexer/lexer.h>
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

int main()
{
    using namespace curlee::types;

    if (!(to_string(TypeKind::Struct) == "Struct" && to_string(TypeKind::Enum) == "Enum" &&
          to_string(Type{.kind = TypeKind::Struct, .name = "S"}) == "S" &&
          to_string(Type{.kind = TypeKind::Enum, .name = "E"}) == "E"))
    {
        fail("expected TypeKind and nominal Type stringification to work");
    }

    if (!(!(Type{.kind = TypeKind::Int} == Type{.kind = TypeKind::Bool}) &&
          (Type{.kind = TypeKind::Struct, .name = "S"} ==
           Type{.kind = TypeKind::Struct, .name = "S"}) &&
                    (Type{.kind = TypeKind::Enum, .name = "E"} == Type{.kind = TypeKind::Enum, .name = "E"}) &&
          !(Type{.kind = TypeKind::Struct, .name = "S"} ==
            Type{.kind = TypeKind::Struct, .name = "T"}) &&
          !(Type{.kind = TypeKind::Enum, .name = "E"} == Type{.kind = TypeKind::Enum, .name = "F"}) &&
          !(Type{.kind = TypeKind::Struct, .name = "S"} ==
            Type{.kind = TypeKind::Enum, .name = "S"})))
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

    std::cout << "OK\n";
    return 0;
}
