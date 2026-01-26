#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/types/type.h>
#include <curlee/types/type_check.h>
#include <string_view>
#include <iostream>

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
        const std::string source = "fn main() -> Int { if 1 { return 0; } return 0; }";

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
        const std::string_view span_text =
            std::string_view(source).substr(d.span.start, d.span.length());
        if (span_text != "1")
        {
            fail("expected condition diagnostic span to cover exactly `1`");
        }
        if (d.message.find("if condition type mismatch") == std::string::npos)
        {
            fail("expected if condition type mismatch diagnostic message");
        }
    }

    std::cout << "OK\n";
    return 0;
}
