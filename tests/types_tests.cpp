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

int main()
{
    using namespace curlee::types;

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

        if (!(ft == same))
        {
            fail("expected function type equality to hold");
        }
        if (ft == different)
        {
            fail("expected different function types to compare unequal");
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

    std::cout << "OK\n";
    return 0;
}
