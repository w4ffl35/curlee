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

int main()
{
    using namespace curlee::types;

    if (!(to_string(TypeKind::Struct) == "Struct" && to_string(TypeKind::Enum) == "Enum" &&
          to_string(TypeKind::Vec) == "Vec" &&
          to_string(Type{.kind = TypeKind::Struct, .name = "S"}) == "S" &&
          to_string(Type{.kind = TypeKind::Enum, .name = "E"}) == "E" &&
          to_string(Type{.kind = TypeKind::Vec,
                         .name = "Vec",
                         .element_kind = TypeKind::Int,
                         .element_name = "Int"}) == "Vec"))
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
        (Type{.kind = TypeKind::Vec,
             .name = "Vec",
             .element_kind = TypeKind::Int,
             .element_name = "Int"} ==
         Type{.kind = TypeKind::Vec,
             .name = "Vec",
             .element_kind = TypeKind::Int,
             .element_name = "Int"}) &&
        !(Type{.kind = TypeKind::Vec,
              .name = "Vec",
              .element_kind = TypeKind::Int,
              .element_name = "Int"} ==
          Type{.kind = TypeKind::Vec,
              .name = "Vec",
              .element_kind = TypeKind::Bool,
               .element_name = "Bool"}) &&
           !(Type{.kind = TypeKind::Vec,
               .name = "Vec",
               .element_kind = TypeKind::Int,
               .element_name = "Int"} ==
             Type{.kind = TypeKind::Vec,
               .name = "Vec",
               .element_kind = TypeKind::Int,
               .element_name = "Num"}) &&
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
        // Builtins: user programs cannot declare builtin function '__read_line'.
        const std::string source =
            "fn __read_line(in: cap io.stdin) -> String { return \"\"; } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __read_line should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__read_line'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__read_line' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__tty_clear'.
        const std::string source =
            "fn __tty_clear(tty: cap io.tty) -> Unit { } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __tty_clear should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__tty_clear'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__tty_clear' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__fs_read_text'.
        const std::string source =
            "fn __fs_read_text(path: String, r: cap fs.read) -> String { return path; } fn "
            "main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __fs_read_text should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__fs_read_text'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__fs_read_text' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__fs_write_text'.
        const std::string source =
            "fn __fs_write_text(path: String, content: String, w: cap fs.write) -> Unit { "
            "} fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __fs_write_text should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__fs_write_text'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__fs_write_text' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__tty_write_at'.
        const std::string source =
            "fn __tty_write_at(row: Int, col: Int, text: String, tty: cap io.tty) -> Unit { } "
            "fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __tty_write_at should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__tty_write_at'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__tty_write_at' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__tty_flush'.
        const std::string source =
            "fn __tty_flush(tty: cap io.tty) -> Unit { } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __tty_flush should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__tty_flush'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__tty_flush' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__rng_next_int'.
        const std::string source =
            "fn __rng_next_int(max_exclusive: Int, rng: cap rng.seeded) -> Int { return 0; } "
            "fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __rng_next_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__rng_next_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__rng_next_int' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__vec_new_int'.
        const std::string source =
            "fn __vec_new_int(max_len: Int) -> Vec { return __vec_new_int(max_len); } "
            "fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __vec_new_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__vec_new_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__vec_new_int' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__vec_len_int'.
        const std::string source =
            "fn __vec_len_int(v: Vec) -> Int { return 0; } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __vec_len_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__vec_len_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__vec_len_int' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__vec_push_int'.
        const std::string source =
            "fn __vec_push_int(v: Vec, x: Int) -> Unit { return; } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __vec_push_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__vec_push_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__vec_push_int' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__vec_get_int'.
        const std::string source =
            "fn __vec_get_int(v: Vec, i: Int) -> Int { return 0; } fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __vec_get_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__vec_get_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__vec_get_int' diagnostic");
        }
    }

    {
        // Builtins: user programs cannot declare builtin function '__vec_set_int'.
        const std::string source =
            "fn __vec_set_int(v: Vec, i: Int, x: Int) -> Unit { return; } "
            "fn main() -> Unit { }";

        const auto diags =
            type_check_should_fail(source, "declaring builtin __vec_set_int should fail");
        bool saw = false;
        for (const auto& d : diags)
        {
            if (d.message.find("cannot declare builtin function '__vec_set_int'") !=
                std::string::npos)
            {
                saw = true;
            }
        }
        if (!saw)
        {
            fail("expected cannot declare builtin function '__vec_set_int' diagnostic");
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
        // Calls: __read_line arity and argument type should be validated.
        const std::string source_arity = "fn main() -> String { return __read_line(); }";
        {
            const auto diags = type_check_should_fail(source_arity, "__read_line arity test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__read_line expects exactly 1 argument") != std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __read_line arity diagnostic");
            }
        }

        const std::string source_arg_type =
            "fn main() -> String { return __read_line(1); }";
        {
            const auto diags =
                type_check_should_fail(source_arg_type, "__read_line argument type test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__read_line expects argument of type cap io.stdin") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __read_line capability argument type diagnostic");
            }
        }

        const std::string source_unknown_arg =
            "fn main() -> String { return __read_line(nope); }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_arg, "__read_line unknown arg test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __read_line unknown-name diagnostic");
            }
        }

        const std::string source_wrong_cap_name =
            "fn main(out: cap io.stdout) -> String { return __read_line(out); }";
        {
            const auto diags = type_check_should_fail(source_wrong_cap_name,
                                                      "__read_line wrong capability name test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__read_line expects argument of type cap io.stdin") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __read_line wrong-capability-name diagnostic");
            }
        }
    }

    {
        // Calls: __fs_read_text arity and argument types should be validated.
        const std::string source_arity =
            "fn main(r: cap fs.read) -> String { return __fs_read_text(r); }";
        {
            const auto diags = type_check_should_fail(source_arity, "__fs_read_text arity test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__fs_read_text expects exactly 2 arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_read_text arity diagnostic");
            }
        }

        const std::string source_arg_type =
            "fn main(r: cap fs.read) -> String { return __fs_read_text(1, r); }";
        {
            const auto diags =
                type_check_should_fail(source_arg_type, "__fs_read_text argument type test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__fs_read_text expects (String, cap fs.read) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_read_text argument type diagnostic");
            }
        }

        const std::string source_unknown_path =
            "fn main(r: cap fs.read) -> String { return __fs_read_text(nope, r); }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_path, "__fs_read_text unknown path test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __fs_read_text unknown-name diagnostic for path argument");
            }
        }

        const std::string source_wrong_cap =
            "fn main(out: cap io.stdout) -> String { return __fs_read_text(\"a.txt\", out); }";
        {
            const auto diags =
                type_check_should_fail(source_wrong_cap, "__fs_read_text wrong capability test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__fs_read_text expects (String, cap fs.read) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_read_text wrong capability diagnostic");
            }
        }

        const std::string source_wrong_cap_with_fs_read =
            "fn main(r: cap fs.read, out: cap io.stdout) -> String { return "
            "__fs_read_text(\"a.txt\", out); }";
        {
            const auto diags = type_check_should_fail(
                source_wrong_cap_with_fs_read,
                "__fs_read_text wrong capability test with fs.read in scope");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__fs_read_text expects (String, cap fs.read) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_read_text wrong-capability diagnostic with fs.read in scope");
            }
        }

        const std::string source_unknown_cap =
            "fn main() -> String { return __fs_read_text(\"a.txt\", nope); }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_cap, "__fs_read_text unknown capability test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __fs_read_text unknown-name diagnostic for capability argument");
            }
        }
    }

    {
        // Calls: __fs_write_text arity and argument types should be validated.
        const std::string source_arity =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a.txt\", w); return; }";
        {
            const auto diags = type_check_should_fail(source_arity, "__fs_write_text arity test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("__fs_write_text expects exactly 3 arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_write_text arity diagnostic");
            }
        }

        const std::string source_arg_type =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a.txt\", 1, w); return; }";
        {
            const auto diags =
                type_check_should_fail(source_arg_type, "__fs_write_text argument type test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find(
                        "__fs_write_text expects (String, String, cap fs.write) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_write_text argument type diagnostic");
            }
        }

        const std::string source_wrong_cap =
            "fn main(r: cap fs.read) -> Unit { __fs_write_text(\"a.txt\", \"x\", r); "
            "return; }";
        {
            const auto diags =
                type_check_should_fail(source_wrong_cap, "__fs_write_text wrong capability test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find(
                        "__fs_write_text expects (String, String, cap fs.write) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_write_text wrong capability diagnostic");
            }
        }

        const std::string source_wrong_cap_with_fs_write =
            "fn main(w: cap fs.write, r: cap fs.read) -> Unit { __fs_write_text(\"a.txt\", "
            "\"x\", r); return; }";
        {
            const auto diags = type_check_should_fail(
                source_wrong_cap_with_fs_write,
                "__fs_write_text wrong capability test with fs.write in scope");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find(
                        "__fs_write_text expects (String, String, cap fs.write) arguments") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected __fs_write_text wrong-capability diagnostic with fs.write in scope");
            }
        }

        const std::string source_unknown_path =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(nope, \"x\", w); return; }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_path, "__fs_write_text unknown path test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __fs_write_text unknown-name diagnostic for path argument");
            }
        }

        const std::string source_unknown_content =
            "fn main(w: cap fs.write) -> Unit { __fs_write_text(\"a.txt\", nope, w); return; }";
        {
            const auto diags = type_check_should_fail(source_unknown_content,
                                                      "__fs_write_text unknown content test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __fs_write_text unknown-name diagnostic for content argument");
            }
        }

        const std::string source_unknown_cap =
            "fn main() -> Unit { __fs_write_text(\"a.txt\", \"x\", nope); return; }";
        {
            const auto diags =
                type_check_should_fail(source_unknown_cap, "__fs_write_text unknown cap test");
            bool saw_unknown_name = false;
            for (const auto& d : diags)
            {
                if (d.message.find("unknown name 'nope'") != std::string::npos)
                {
                    saw_unknown_name = true;
                }
            }
            if (!saw_unknown_name)
            {
                fail("expected __fs_write_text unknown-name diagnostic for capability argument");
            }
        }
    }

    {
        const auto info = type_check_should_succeed(
            "fn main(in: cap io.stdin) -> String { return __read_line(in); }",
            "__read_line capability tracking");

        bool saw_required_stdin = false;
        for (const auto& req : info.required_capabilities)
        {
            if (req.name == "io.stdin")
            {
                saw_required_stdin = true;
                break;
            }
        }

        if (!saw_required_stdin)
        {
            fail("expected required capabilities to include io.stdin for __read_line");
        }
    }

    {
        // Calls: __tty_* argument and arity checks.
        const auto diags_arity =
            type_check_should_fail("fn main(tty: cap io.tty) -> Unit { __tty_clear(); }",
                                   "__tty_clear arity test");
        bool saw_arity = false;
        for (const auto& d : diags_arity)
        {
            if (d.message.find("__tty_clear expects exactly 1 argument") != std::string::npos)
            {
                saw_arity = true;
            }
        }
        if (!saw_arity)
        {
            fail("expected __tty_clear arity diagnostic");
        }

        const auto diags_clear_type =
            type_check_should_fail("fn main() -> Unit { __tty_clear(1); }",
                                   "__tty_clear arg type test");
        bool saw_clear_type = false;
        for (const auto& d : diags_clear_type)
        {
            if (d.message.find("__tty_clear expects argument of type cap io.tty") !=
                std::string::npos)
            {
                saw_clear_type = true;
            }
        }
        if (!saw_clear_type)
        {
            fail("expected __tty_clear argument type diagnostic");
        }

        const auto diags_clear_unknown =
            type_check_should_fail("fn main() -> Unit { __tty_clear(nope); }",
                                   "__tty_clear unknown arg test");
        bool saw_clear_unknown = false;
        for (const auto& d : diags_clear_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_clear_unknown = true;
            }
        }
        if (!saw_clear_unknown)
        {
            fail("expected __tty_clear unknown-name diagnostic");
        }

        const auto diags_clear_name =
            type_check_should_fail("fn main(out: cap io.stdout) -> Unit { __tty_clear(out); }",
                                   "__tty_clear capability name test");
        bool saw_clear_name = false;
        for (const auto& d : diags_clear_name)
        {
            if (d.message.find("__tty_clear expects argument of type cap io.tty") !=
                std::string::npos)
            {
                saw_clear_name = true;
            }
        }
        if (!saw_clear_name)
        {
            fail("expected __tty_clear capability-name diagnostic");
        }

        const auto diags_sig = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, 1, 2, tty); }",
            "__tty_write_at signature test");
        bool saw_sig = false;
        for (const auto& d : diags_sig)
        {
            if (d.message.find("__tty_write_at expects (Int, Int, String, cap io.tty) arguments") !=
                std::string::npos)
            {
                saw_sig = true;
            }
        }
        if (!saw_sig)
        {
            fail("expected __tty_write_at signature diagnostic");
        }

        const auto diags_col_type = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, true, \"x\", tty); }",
            "__tty_write_at column type test");
        bool saw_col_type = false;
        for (const auto& d : diags_col_type)
        {
            if (d.message.find("__tty_write_at expects (Int, Int, String, cap io.tty) arguments") !=
                std::string::npos)
            {
                saw_col_type = true;
            }
        }
        if (!saw_col_type)
        {
            fail("expected __tty_write_at column type diagnostic");
        }

        const auto diags_tty_kind = type_check_should_fail(
            "fn main() -> Unit { __tty_write_at(0, 0, \"x\", 1); }",
            "__tty_write_at tty kind test");
        bool saw_tty_kind = false;
        for (const auto& d : diags_tty_kind)
        {
            if (d.message.find("__tty_write_at expects (Int, Int, String, cap io.tty) arguments") !=
                std::string::npos)
            {
                saw_tty_kind = true;
            }
        }
        if (!saw_tty_kind)
        {
            fail("expected __tty_write_at tty-kind diagnostic");
        }

        const auto diags_tty_name = type_check_should_fail(
            "fn main(out: cap io.stdout) -> Unit { __tty_write_at(0, 0, \"x\", out); }",
            "__tty_write_at tty name test");
        bool saw_tty_name = false;
        for (const auto& d : diags_tty_name)
        {
            if (d.message.find("__tty_write_at expects (Int, Int, String, cap io.tty) arguments") !=
                std::string::npos)
            {
                saw_tty_name = true;
            }
        }
        if (!saw_tty_name)
        {
            fail("expected __tty_write_at tty-name diagnostic");
        }

        const auto diags_row_type = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(true, 0, \"x\", tty); }",
            "__tty_write_at row type test");
        bool saw_row_type = false;
        for (const auto& d : diags_row_type)
        {
            if (d.message.find("__tty_write_at expects (Int, Int, String, cap io.tty) arguments") !=
                std::string::npos)
            {
                saw_row_type = true;
            }
        }
        if (!saw_row_type)
        {
            fail("expected __tty_write_at row type diagnostic");
        }

        const auto diags_col_unknown = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, nope, \"x\", tty); }",
            "__tty_write_at unknown col test");
        bool saw_col_unknown = false;
        for (const auto& d : diags_col_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_col_unknown = true;
            }
        }
        if (!saw_col_unknown)
        {
            fail("expected __tty_write_at unknown-column diagnostic");
        }

        const auto diags_text_unknown = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, 0, nope, tty); }",
            "__tty_write_at unknown text test");
        bool saw_text_unknown = false;
        for (const auto& d : diags_text_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_text_unknown = true;
            }
        }
        if (!saw_text_unknown)
        {
            fail("expected __tty_write_at unknown-text diagnostic");
        }

        const auto diags_tty_unknown = type_check_should_fail(
            "fn main() -> Unit { __tty_write_at(0, 0, \"x\", nope); }",
            "__tty_write_at unknown tty test");
        bool saw_tty_unknown = false;
        for (const auto& d : diags_tty_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_tty_unknown = true;
            }
        }
        if (!saw_tty_unknown)
        {
            fail("expected __tty_write_at unknown-tty diagnostic");
        }

        const auto diags_write_arity =
            type_check_should_fail("fn main(tty: cap io.tty) -> Unit { __tty_write_at(0, 0, \"x\"); }",
                                   "__tty_write_at arity test");
        bool saw_write_arity = false;
        for (const auto& d : diags_write_arity)
        {
            if (d.message.find("__tty_write_at expects exactly 4 arguments") != std::string::npos)
            {
                saw_write_arity = true;
            }
        }
        if (!saw_write_arity)
        {
            fail("expected __tty_write_at arity diagnostic");
        }

        const auto diags_write_unknown = type_check_should_fail(
            "fn main(tty: cap io.tty) -> Unit { __tty_write_at(nope, 0, \"x\", tty); }",
            "__tty_write_at unknown arg test");
        bool saw_write_unknown = false;
        for (const auto& d : diags_write_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_write_unknown = true;
            }
        }
        if (!saw_write_unknown)
        {
            fail("expected __tty_write_at unknown-name diagnostic");
        }

        const auto diags_flush_arity =
            type_check_should_fail("fn main(tty: cap io.tty) -> Unit { __tty_flush(); }",
                                   "__tty_flush arity test");
        bool saw_flush_arity = false;
        for (const auto& d : diags_flush_arity)
        {
            if (d.message.find("__tty_flush expects exactly 1 argument") != std::string::npos)
            {
                saw_flush_arity = true;
            }
        }
        if (!saw_flush_arity)
        {
            fail("expected __tty_flush arity diagnostic");
        }

        const auto diags_flush_type =
            type_check_should_fail("fn main() -> Unit { __tty_flush(1); }",
                                   "__tty_flush arg type test");
        bool saw_flush_type = false;
        for (const auto& d : diags_flush_type)
        {
            if (d.message.find("__tty_flush expects argument of type cap io.tty") !=
                std::string::npos)
            {
                saw_flush_type = true;
            }
        }
        if (!saw_flush_type)
        {
            fail("expected __tty_flush argument type diagnostic");
        }

        const auto diags_flush_name =
            type_check_should_fail("fn main(out: cap io.stdout) -> Unit { __tty_flush(out); }",
                                   "__tty_flush capability name test");
        bool saw_flush_name = false;
        for (const auto& d : diags_flush_name)
        {
            if (d.message.find("__tty_flush expects argument of type cap io.tty") !=
                std::string::npos)
            {
                saw_flush_name = true;
            }
        }
        if (!saw_flush_name)
        {
            fail("expected __tty_flush capability-name diagnostic");
        }

        const auto diags_flush_unknown =
            type_check_should_fail("fn main() -> Unit { __tty_flush(nope); }",
                                   "__tty_flush unknown arg test");
        bool saw_flush_unknown = false;
        for (const auto& d : diags_flush_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_flush_unknown = true;
            }
        }
        if (!saw_flush_unknown)
        {
            fail("expected __tty_flush unknown-name diagnostic");
        }

        const auto info = type_check_should_succeed(
            "fn main(tty: cap io.tty) -> Unit { __tty_clear(tty); __tty_write_at(0, 0, \"x\", "
            "tty); __tty_flush(tty); }",
            "__tty_* capability tracking");

        bool saw_required_tty = false;
        for (const auto& req : info.required_capabilities)
        {
            if (req.name == "io.tty")
            {
                saw_required_tty = true;
                break;
            }
        }
        if (!saw_required_tty)
        {
            fail("expected required capabilities to include io.tty for __tty_* builtins");
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
        const auto diags_rng_arity =
            type_check_should_fail("fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(1); }",
                                   "__rng_next_int arity test");
        bool saw_rng_arity = false;
        for (const auto& d : diags_rng_arity)
        {
            if (d.message.find("__rng_next_int expects exactly 2 arguments") !=
                std::string::npos)
            {
                saw_rng_arity = true;
            }
        }
        if (!saw_rng_arity)
        {
            fail("expected __rng_next_int arity diagnostic");
        }

        const auto diags_rng_arg_type =
            type_check_should_fail("fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(true, rng); }",
                                   "__rng_next_int max_exclusive type test");
        bool saw_rng_arg_type = false;
        for (const auto& d : diags_rng_arg_type)
        {
            if (d.message.find("__rng_next_int expects (Int, cap rng.seeded) arguments") !=
                std::string::npos)
            {
                saw_rng_arg_type = true;
            }
        }
        if (!saw_rng_arg_type)
        {
            fail("expected __rng_next_int argument type diagnostic");
        }

        const auto diags_rng_cap_type =
            type_check_should_fail("fn main() -> Int { return __rng_next_int(10, 1); }",
                                   "__rng_next_int capability arg type test");
        bool saw_rng_cap_type = false;
        for (const auto& d : diags_rng_cap_type)
        {
            if (d.message.find("__rng_next_int expects (Int, cap rng.seeded) arguments") !=
                std::string::npos)
            {
                saw_rng_cap_type = true;
            }
        }
        if (!saw_rng_cap_type)
        {
            fail("expected __rng_next_int capability argument diagnostic");
        }

        const auto diags_rng_cap_name =
            type_check_should_fail(
                "fn main(out: cap io.stdout) -> Int { return __rng_next_int(10, out); }",
                "__rng_next_int capability name test");
        bool saw_rng_cap_name = false;
        for (const auto& d : diags_rng_cap_name)
        {
            if (d.message.find("__rng_next_int expects (Int, cap rng.seeded) arguments") !=
                std::string::npos)
            {
                saw_rng_cap_name = true;
            }
        }
        if (!saw_rng_cap_name)
        {
            fail("expected __rng_next_int capability-name diagnostic");
        }

        const auto diags_rng_unknown_first =
            type_check_should_fail("fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(nope, rng); }",
                                   "__rng_next_int unknown max_exclusive test");
        bool saw_rng_unknown_first = false;
        for (const auto& d : diags_rng_unknown_first)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_rng_unknown_first = true;
            }
        }
        if (!saw_rng_unknown_first)
        {
            fail("expected __rng_next_int unknown max_exclusive diagnostic");
        }

        const auto diags_rng_unknown_second =
            type_check_should_fail("fn main() -> Int { return __rng_next_int(10, nope); }",
                                   "__rng_next_int unknown capability arg test");
        bool saw_rng_unknown_second = false;
        for (const auto& d : diags_rng_unknown_second)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_rng_unknown_second = true;
            }
        }
        if (!saw_rng_unknown_second)
        {
            fail("expected __rng_next_int unknown capability argument diagnostic");
        }

        const auto info = type_check_should_succeed(
            "fn main(rng: cap rng.seeded) -> Int { return __rng_next_int(10, rng); }",
            "__rng_next_int capability tracking");

        bool saw_required_rng = false;
        for (const auto& req : info.required_capabilities)
        {
            if (req.name == "rng.seeded")
            {
                saw_required_rng = true;
                break;
            }
        }
        if (!saw_required_rng)
        {
            fail("expected required capabilities to include rng.seeded for __rng_next_int");
        }
    }

    {
        const auto diags_vec_new_arity =
            type_check_should_fail("fn main() -> Unit { __vec_new_int(1, 2); return; }",
                                   "__vec_new_int arity test");
        bool saw_vec_new_arity = false;
        for (const auto& d : diags_vec_new_arity)
        {
            if (d.message.find("__vec_new_int expects exactly 1 argument") !=
                std::string::npos)
            {
                saw_vec_new_arity = true;
            }
        }
        if (!saw_vec_new_arity)
        {
            fail("expected __vec_new_int arity diagnostic");
        }

        const auto diags_vec_new_type =
            type_check_should_fail("fn main() -> Unit { __vec_new_int(true); return; }",
                                   "__vec_new_int type test");
        bool saw_vec_new_type = false;
        for (const auto& d : diags_vec_new_type)
        {
            if (d.message.find("__vec_new_int expects argument of type Int") !=
                std::string::npos)
            {
                saw_vec_new_type = true;
            }
        }
        if (!saw_vec_new_type)
        {
            fail("expected __vec_new_int type diagnostic");
        }

        const auto diags_vec_new_unknown =
            type_check_should_fail("fn main() -> Unit { __vec_new_int(nope); return; }",
                                   "__vec_new_int unknown arg test");
        bool saw_vec_new_unknown = false;
        for (const auto& d : diags_vec_new_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_new_unknown = true;
            }
        }
        if (!saw_vec_new_unknown)
        {
            fail("expected __vec_new_int unknown-name diagnostic");
        }

        const auto diags_vec_len_arity =
            type_check_should_fail("fn main() -> Int { return __vec_len_int(); }",
                                   "__vec_len_int arity test");
        bool saw_vec_len_arity = false;
        for (const auto& d : diags_vec_len_arity)
        {
            if (d.message.find("__vec_len_int expects exactly 1 argument") != std::string::npos)
            {
                saw_vec_len_arity = true;
            }
        }
        if (!saw_vec_len_arity)
        {
            fail("expected __vec_len_int arity diagnostic");
        }

        const auto diags_vec_len_sig =
            type_check_should_fail("fn main() -> Int { return __vec_len_int(1); }",
                                   "__vec_len_int signature test");
        bool saw_vec_len_sig = false;
        for (const auto& d : diags_vec_len_sig)
        {
            if (d.message.find("__vec_len_int expects argument of type Vec<Int>") !=
                std::string::npos)
            {
                saw_vec_len_sig = true;
            }
        }
        if (!saw_vec_len_sig)
        {
            fail("expected __vec_len_int signature diagnostic");
        }

        const auto diags_vec_len_unknown =
            type_check_should_fail("fn main() -> Int { return __vec_len_int(nope); }",
                                   "__vec_len_int unknown arg test");
        bool saw_vec_len_unknown = false;
        for (const auto& d : diags_vec_len_unknown)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_len_unknown = true;
            }
        }
        if (!saw_vec_len_unknown)
        {
            fail("expected __vec_len_int unknown-name diagnostic");
        }

        const auto diags_vec_bad_type_arg =
            type_check_should_fail(
                "fn main(v: Vec<Bool>) -> Int { return __vec_len_int(v); }",
                "Vec<Bool> unsupported type argument test");
        bool saw_vec_bad_type_arg = false;
        for (const auto& d : diags_vec_bad_type_arg)
        {
            if (d.message.find("only Vec<Int> is supported in MVP-run") != std::string::npos)
            {
                saw_vec_bad_type_arg = true;
            }
        }
        if (!saw_vec_bad_type_arg)
        {
            fail("expected Vec<Bool> unsupported-type-argument diagnostic");
        }

        const auto info_vec_int = type_check_should_succeed(
            "fn main(v: Vec<Int>) -> Int { return __vec_len_int(v); }",
            "Vec<Int> explicit type argument test");
        if (!info_vec_int.required_capabilities.empty())
        {
            fail("expected Vec<Int> explicit type-arg program to require no capabilities");
        }

        const auto diags_vec_push_arity =
            type_check_should_fail("fn main() -> Unit { __vec_push_int(__vec_new_int(1)); return; }",
                                   "__vec_push_int arity test");
        bool saw_vec_push_arity = false;
        for (const auto& d : diags_vec_push_arity)
        {
            if (d.message.find("__vec_push_int expects exactly 2 arguments") !=
                std::string::npos)
            {
                saw_vec_push_arity = true;
            }
        }
        if (!saw_vec_push_arity)
        {
            fail("expected __vec_push_int arity diagnostic");
        }

        const auto diags_vec_push_sig =
            type_check_should_fail("fn main() -> Unit { __vec_push_int(1, true); return; }",
                                   "__vec_push_int signature test");
        bool saw_vec_push_sig = false;
        for (const auto& d : diags_vec_push_sig)
        {
            if (d.message.find("__vec_push_int expects (Vec<Int>, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_push_sig = true;
            }
        }
        if (!saw_vec_push_sig)
        {
            fail("expected __vec_push_int signature diagnostic");
        }

        const auto diags_vec_push_sig_value =
            type_check_should_fail(
                "fn main() -> Unit { __vec_push_int(__vec_new_int(1), true); return; }",
                "__vec_push_int value signature test");
        bool saw_vec_push_sig_value = false;
        for (const auto& d : diags_vec_push_sig_value)
        {
            if (d.message.find("__vec_push_int expects (Vec<Int>, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_push_sig_value = true;
            }
        }
        if (!saw_vec_push_sig_value)
        {
            fail("expected __vec_push_int value signature diagnostic");
        }

        const auto diags_vec_push_unknown_first =
            type_check_should_fail("fn main() -> Unit { __vec_push_int(nope, 1); return; }",
                                   "__vec_push_int unknown vec arg test");
        bool saw_vec_push_unknown_first = false;
        for (const auto& d : diags_vec_push_unknown_first)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_push_unknown_first = true;
            }
        }
        if (!saw_vec_push_unknown_first)
        {
            fail("expected __vec_push_int unknown vec argument diagnostic");
        }

        const auto diags_vec_push_unknown_second =
            type_check_should_fail(
                "fn main() -> Unit { let v: Vec = __vec_new_int(1); __vec_push_int(v, nope); "
                "return; }",
                "__vec_push_int unknown value arg test");
        bool saw_vec_push_unknown_second = false;
        for (const auto& d : diags_vec_push_unknown_second)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_push_unknown_second = true;
            }
        }
        if (!saw_vec_push_unknown_second)
        {
            fail("expected __vec_push_int unknown value argument diagnostic");
        }

        const auto diags_vec_get_arity =
            type_check_should_fail("fn main() -> Int { return __vec_get_int(__vec_new_int(1)); }",
                                   "__vec_get_int arity test");
        bool saw_vec_get_arity = false;
        for (const auto& d : diags_vec_get_arity)
        {
            if (d.message.find("__vec_get_int expects exactly 2 arguments") != std::string::npos)
            {
                saw_vec_get_arity = true;
            }
        }
        if (!saw_vec_get_arity)
        {
            fail("expected __vec_get_int arity diagnostic");
        }

        const auto diags_vec_get_sig =
            type_check_should_fail("fn main() -> Int { return __vec_get_int(__vec_new_int(1), true); }",
                                   "__vec_get_int signature test");
        bool saw_vec_get_sig = false;
        for (const auto& d : diags_vec_get_sig)
        {
            if (d.message.find("__vec_get_int expects (Vec<Int>, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_get_sig = true;
            }
        }
        if (!saw_vec_get_sig)
        {
            fail("expected __vec_get_int signature diagnostic");
        }

        const auto diags_vec_get_sig_vec =
            type_check_should_fail("fn main() -> Int { return __vec_get_int(1, 0); }",
                                   "__vec_get_int vec signature test");
        bool saw_vec_get_sig_vec = false;
        for (const auto& d : diags_vec_get_sig_vec)
        {
            if (d.message.find("__vec_get_int expects (Vec<Int>, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_get_sig_vec = true;
            }
        }
        if (!saw_vec_get_sig_vec)
        {
            fail("expected __vec_get_int vec signature diagnostic");
        }

        const auto diags_vec_get_unknown_first =
            type_check_should_fail("fn main() -> Int { return __vec_get_int(nope, 0); }",
                                   "__vec_get_int unknown vec arg test");
        bool saw_vec_get_unknown_first = false;
        for (const auto& d : diags_vec_get_unknown_first)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_get_unknown_first = true;
            }
        }
        if (!saw_vec_get_unknown_first)
        {
            fail("expected __vec_get_int unknown vec argument diagnostic");
        }

        const auto diags_vec_get_unknown_second =
            type_check_should_fail(
                "fn main() -> Int { let v: Vec = __vec_new_int(1); return __vec_get_int(v, nope); }",
                "__vec_get_int unknown index arg test");
        bool saw_vec_get_unknown_second = false;
        for (const auto& d : diags_vec_get_unknown_second)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_get_unknown_second = true;
            }
        }
        if (!saw_vec_get_unknown_second)
        {
            fail("expected __vec_get_int unknown index argument diagnostic");
        }

        const auto diags_vec_set_arity =
            type_check_should_fail("fn main() -> Unit { __vec_set_int(__vec_new_int(1), 0); return; }",
                                   "__vec_set_int arity test");
        bool saw_vec_set_arity = false;
        for (const auto& d : diags_vec_set_arity)
        {
            if (d.message.find("__vec_set_int expects exactly 3 arguments") != std::string::npos)
            {
                saw_vec_set_arity = true;
            }
        }
        if (!saw_vec_set_arity)
        {
            fail("expected __vec_set_int arity diagnostic");
        }

        const auto diags_vec_set_sig =
            type_check_should_fail(
                "fn main() -> Unit { __vec_set_int(__vec_new_int(1), true, 1); return; }",
                "__vec_set_int signature test");
        bool saw_vec_set_sig = false;
        for (const auto& d : diags_vec_set_sig)
        {
            if (d.message.find("__vec_set_int expects (Vec<Int>, Int, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_set_sig = true;
            }
        }
        if (!saw_vec_set_sig)
        {
            fail("expected __vec_set_int signature diagnostic");
        }

        const auto diags_vec_set_sig_vec =
            type_check_should_fail("fn main() -> Unit { __vec_set_int(1, 0, 1); return; }",
                                   "__vec_set_int vec signature test");
        bool saw_vec_set_sig_vec = false;
        for (const auto& d : diags_vec_set_sig_vec)
        {
            if (d.message.find("__vec_set_int expects (Vec<Int>, Int, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_set_sig_vec = true;
            }
        }
        if (!saw_vec_set_sig_vec)
        {
            fail("expected __vec_set_int vec signature diagnostic");
        }

        const auto diags_vec_set_sig_value =
            type_check_should_fail(
                "fn main() -> Unit { __vec_set_int(__vec_new_int(1), 0, true); return; }",
                "__vec_set_int value signature test");
        bool saw_vec_set_sig_value = false;
        for (const auto& d : diags_vec_set_sig_value)
        {
            if (d.message.find("__vec_set_int expects (Vec<Int>, Int, Int) arguments") !=
                std::string::npos)
            {
                saw_vec_set_sig_value = true;
            }
        }
        if (!saw_vec_set_sig_value)
        {
            fail("expected __vec_set_int value signature diagnostic");
        }

        const auto diags_vec_set_unknown_first =
            type_check_should_fail("fn main() -> Unit { __vec_set_int(nope, 0, 1); return; }",
                                   "__vec_set_int unknown vec arg test");
        bool saw_vec_set_unknown_first = false;
        for (const auto& d : diags_vec_set_unknown_first)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_set_unknown_first = true;
            }
        }
        if (!saw_vec_set_unknown_first)
        {
            fail("expected __vec_set_int unknown vec argument diagnostic");
        }

        const auto diags_vec_set_unknown_second =
            type_check_should_fail(
                "fn main() -> Unit { let v: Vec = __vec_new_int(1); __vec_set_int(v, nope, 1); "
                "return; }",
                "__vec_set_int unknown index arg test");
        bool saw_vec_set_unknown_second = false;
        for (const auto& d : diags_vec_set_unknown_second)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_set_unknown_second = true;
            }
        }
        if (!saw_vec_set_unknown_second)
        {
            fail("expected __vec_set_int unknown index argument diagnostic");
        }

        const auto diags_vec_set_unknown_third =
            type_check_should_fail(
                "fn main() -> Unit { let v: Vec = __vec_new_int(1); __vec_set_int(v, 0, nope); "
                "return; }",
                "__vec_set_int unknown value arg test");
        bool saw_vec_set_unknown_third = false;
        for (const auto& d : diags_vec_set_unknown_third)
        {
            if (d.message.find("unknown name 'nope'") != std::string::npos)
            {
                saw_vec_set_unknown_third = true;
            }
        }
        if (!saw_vec_set_unknown_third)
        {
            fail("expected __vec_set_int unknown value argument diagnostic");
        }

        const auto info = type_check_should_succeed(
            "fn main() -> Int { let v: Vec = __vec_new_int(4); __vec_push_int(v, 2); "
            "__vec_set_int(v, 0, 5); return __vec_get_int(v, 0); }",
            "vector builtins happy path");
        if (info.required_capabilities.size() != 0)
        {
            fail("expected vector builtins to require no capabilities");
        }
    }

    {
        // python_ffi surface is de-scoped from v1.
        const std::string source_requires_unsafe = "fn main() -> Unit { python_ffi.call(); }";
        {
            const auto diags =
                type_check_should_fail(source_requires_unsafe, "python_ffi unsafe test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("python_ffi is not part of the Curlee v1 surface") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected python_ffi unsupported-surface diagnostic");
            }
        }

        const std::string source_args = "fn main() -> Unit { unsafe { python_ffi.call(1); } }";
        {
            const auto diags = type_check_should_fail(source_args, "python_ffi args test");
            bool saw = false;
            for (const auto& d : diags)
            {
                if (d.message.find("python_ffi is not part of the Curlee v1 surface") !=
                    std::string::npos)
                {
                    saw = true;
                }
            }
            if (!saw)
            {
                fail("expected python_ffi unsupported-surface diagnostic");
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

    std::cout << "OK\n";
    return 0;
}
