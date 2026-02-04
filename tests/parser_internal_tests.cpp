#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <curlee/lexer/token.h>
#include <curlee/parser/parser.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

static void fail(std::string_view msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static curlee::lexer::Token tok(curlee::lexer::TokenKind kind, std::string_view lex,
                                std::size_t start)
{
    return curlee::lexer::Token{
        .kind = kind,
        .lexeme = lex,
        .span = curlee::source::Span{.start = start, .end = start + 1},
    };
}

static std::vector<curlee::lexer::Token>
make_tokens(std::initializer_list<std::pair<curlee::lexer::TokenKind, std::string_view>> items)
{
    std::vector<curlee::lexer::Token> out;
    std::size_t pos = 0;
    for (const auto& [k, lex] : items)
    {
        out.push_back(tok(k, lex, pos++));
    }
    out.push_back(tok(curlee::lexer::TokenKind::Eof, "", pos));
    return out;
}

// Pull in parser.cpp internals for deterministic, direct coverage of private helpers.
// We include public headers above so the private->public macro does not affect them.
#define private public
#include "../src/parser/parser.cpp"
#undef private

int main()
{
    using curlee::diag::Diagnostic;
    using curlee::lexer::TokenKind;

    auto expect_diag = [](const auto& v)
    {
        if (!std::holds_alternative<Diagnostic>(v))
        {
            fail("expected Diagnostic");
        }
    };

    // --- Top-level decl parsers: force consume()/shape errors that are unreachable via parse().
    {
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_import());
    }

    {
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_struct_decl());
    }

    {
        auto toks = make_tokens({{TokenKind::KwStruct, "struct"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_struct_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwStruct, "struct"},
            {TokenKind::Identifier, "S"},
            {TokenKind::LBrace, "{"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_struct_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwStruct, "struct"},
            {TokenKind::Identifier, "S"},
            {TokenKind::LBrace, "{"},
            {TokenKind::Identifier, "field"},
            {TokenKind::Colon, ":"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_struct_decl());
    }

    {
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        auto toks = make_tokens({{TokenKind::KwEnum, "enum"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwEnum, "enum"},
            {TokenKind::Identifier, "E"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwEnum, "enum"},
            {TokenKind::Identifier, "E"},
            {TokenKind::LBrace, "{"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwEnum, "enum"},
            {TokenKind::Identifier, "E"},
            {TokenKind::LBrace, "{"},
            {TokenKind::Identifier, "V"},
            {TokenKind::LParen, "("},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_function());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwFn, "fn"},
            {TokenKind::Identifier, "f"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_function());
    }

    {
        // return type parse_type() error after '->'
        auto toks = make_tokens({
            {TokenKind::KwFn, "fn"},
            {TokenKind::Identifier, "f"},
            {TokenKind::LParen, "("},
            {TokenKind::RParen, ")"},
            {TokenKind::Arrow, "->"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_function());
    }

    {
        // contract ensures: predicate parse error
        auto toks = make_tokens({
            {TokenKind::KwFn, "fn"},
            {TokenKind::Identifier, "f"},
            {TokenKind::LParen, "("},
            {TokenKind::RParen, ")"},
            {TokenKind::Arrow, "->"},
            {TokenKind::Identifier, "Unit"},
            {TokenKind::LBracket, "["},
            {TokenKind::KwEnsures, "ensures"},
            {TokenKind::Semicolon, ";"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_function());
    }

    // --- Predicate rhs error propagation in each precedence tier.
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::OrOr, "||"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_or());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::AndAnd, "&&"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_and());
    }
    {
        auto toks = make_tokens({{TokenKind::IntLiteral, "1"},
                                 {TokenKind::EqualEqual, "=="},
                                 {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_equality());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Less, "<"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_comparison());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Plus, "+"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_term());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Star, "*"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_factor());
    }
    {
        auto toks = make_tokens({{TokenKind::Bang, "!"}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_unary());
    }
    {
        // Pred group: inner predicate error path.
        auto toks = make_tokens({{TokenKind::LParen, "("}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_pred_primary());
    }

    // --- Statement parsing: block/unsafe/if/while error propagation.
    {
        auto toks = make_tokens({{TokenKind::LBrace, "{"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({{TokenKind::KwUnsafe, "unsafe"}, {TokenKind::LBrace, "{"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwLet, "let"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwLet, "let"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
            {TokenKind::KwWhere, "where"},
            {TokenKind::Semicolon, ";"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwLet, "let"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
            {TokenKind::IntLiteral, "0"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwLet, "let"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
            {TokenKind::Equal, "="},
            {TokenKind::IntLiteral, "1"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwIf, "if"},
            {TokenKind::LParen, "("},
            {TokenKind::RParen, ")"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwIf, "if"},
            {TokenKind::LParen, "("},
            {TokenKind::KwTrue, "true"},
            {TokenKind::RParen, ")"},
            {TokenKind::LBrace, "{"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwWhile, "while"},
            {TokenKind::LParen, "("},
            {TokenKind::RParen, ")"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwWhile, "while"},
            {TokenKind::LParen, "("},
            {TokenKind::KwTrue, "true"},
            {TokenKind::LBrace, "{"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    {
        auto toks = make_tokens({{TokenKind::KwReturn, "return"},
                                 {TokenKind::RParen, ")"},
                                 {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    // --- Expression rhs error propagation in each precedence tier.
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::OrOr, "||"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_or());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::AndAnd, "&&"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_and());
    }
    {
        auto toks = make_tokens({{TokenKind::IntLiteral, "1"},
                                 {TokenKind::EqualEqual, "=="},
                                 {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_equality());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Less, "<"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_comparison());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Plus, "+"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_term());
    }
    {
        auto toks = make_tokens(
            {{TokenKind::IntLiteral, "1"}, {TokenKind::Star, "*"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_factor());
    }
    {
        auto toks = make_tokens({{TokenKind::Bang, "!"}, {TokenKind::Semicolon, ";"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_unary());
    }

    {
        // call arg parse error
        auto toks = make_tokens({
            {TokenKind::Identifier, "f"},
            {TokenKind::LParen, "("},
            {TokenKind::Comma, ","},
            {TokenKind::RParen, ")"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_call());
    }

    {
        // call missing ')' after arguments
        auto toks = make_tokens({
            {TokenKind::Identifier, "f"},
            {TokenKind::LParen, "("},
            {TokenKind::IntLiteral, "1"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_call());
    }

    {
        // struct literal: force the consume('{') error path (unreachable via parse_primary).
        auto toks = make_tokens({{TokenKind::Eof, ""}});
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    {
        // struct literal: field name missing.
        auto toks = make_tokens(
            {{TokenKind::LBrace, "{"}, {TokenKind::IntLiteral, "0"}, {TokenKind::RBrace, "}"}});
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    {
        // struct literal: missing ':' after field name.
        auto toks = make_tokens(
            {{TokenKind::LBrace, "{"}, {TokenKind::Identifier, "x"}, {TokenKind::IntLiteral, "0"}});
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    {
        // struct literal: field value parse error.
        auto toks = make_tokens({{TokenKind::LBrace, "{"},
                                 {TokenKind::Identifier, "x"},
                                 {TokenKind::Colon, ":"},
                                 {TokenKind::Comma, ","}});
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    {
        // struct literal: missing '}' after struct literal.
        auto toks = make_tokens({{TokenKind::LBrace, "{"}});
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    {
        // scoped name: missing identifier after '::'
        auto toks = make_tokens({{TokenKind::Identifier, "A"}, {TokenKind::ColonColon, "::"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_primary());
    }

    {
        // group expr: inner expr error
        auto toks = make_tokens({{TokenKind::LParen, "("}, {TokenKind::RParen, ")"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_primary());
    }

    {
        // group expr: missing ')'
        auto toks = make_tokens({{TokenKind::LParen, "("}, {TokenKind::IntLiteral, "1"}});
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_primary());
    }

    // --- Parser utility edge cases (hard to reach via public parse()).
    {
        // advance() when already at EOF should not increment pos_.
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        p.pos_ = 1; // points at EOF
        (void)p.advance();
    }

    {
        // match() false branch.
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        if (p.match(TokenKind::KwFn))
        {
            fail("expected match(KwFn) to be false");
        }
    }

    {
        // synchronize_stmt() when at EOF.
        auto toks = make_tokens({{TokenKind::Identifier, "x"}});
        curlee::parser::Parser p(toks);
        p.pos_ = 1; // EOF
        p.synchronize_stmt();
    }

    {
        // synchronize_top_level() should skip junk until top-level keyword.
        auto toks = make_tokens({{TokenKind::IntLiteral, "0"},
                                 {TokenKind::IntLiteral, "1"},
                                 {TokenKind::KwFn, "fn"},
                                 {TokenKind::Identifier, "f"}});
        curlee::parser::Parser p(toks);
        p.synchronize_top_level();
    }

    {
        auto toks = make_tokens({{TokenKind::IntLiteral, "0"}, {TokenKind::KwImport, "import"}});
        curlee::parser::Parser p(toks);
        p.synchronize_top_level();
    }

    {
        auto toks = make_tokens({{TokenKind::IntLiteral, "0"}, {TokenKind::KwStruct, "struct"}});
        curlee::parser::Parser p(toks);
        p.synchronize_top_level();
    }

    {
        auto toks = make_tokens({{TokenKind::IntLiteral, "0"}, {TokenKind::KwEnum, "enum"}});
        curlee::parser::Parser p(toks);
        p.synchronize_top_level();
    }

    {
        // Runs to EOF (no top-level keyword found).
        auto toks = make_tokens({{TokenKind::IntLiteral, "0"}, {TokenKind::IntLiteral, "1"}});
        curlee::parser::Parser p(toks);
        p.synchronize_top_level();
    }

    {
        // assign_expr_ids(): cover the "null field value" branch for StructLiteralExpr.
        // This is not constructible via parsing (parser always sets value), but the traversal
        // is defensive so we exercise it via a manually-built AST.
        curlee::parser::Program program;

        std::vector<curlee::parser::StructLiteralExprField> fields;
        {
            curlee::parser::StructLiteralExprField field;
            field.span = curlee::source::Span{.start = 0, .end = 0};
            field.name = "x";
            field.value = nullptr;
            fields.push_back(std::move(field));
        }

        curlee::parser::Expr struct_lit;
        struct_lit.span = curlee::source::Span{.start = 0, .end = 0};
        struct_lit.node = curlee::parser::StructLiteralExpr{.type_name = "T",
                                                            .fields = std::move(fields)};

        curlee::parser::Stmt stmt;
        stmt.span = curlee::source::Span{.start = 0, .end = 0};
        stmt.node = curlee::parser::ExprStmt{.expr = std::move(struct_lit)};

        curlee::parser::Block body;
        body.span = curlee::source::Span{.start = 0, .end = 0};
        body.stmts.push_back(std::move(stmt));

        curlee::parser::Function fun;
        fun.span = curlee::source::Span{.start = 0, .end = 0};
        fun.name = "f";
        fun.body = std::move(body);
        program.functions.push_back(std::move(fun));

        curlee::parser::reassign_expr_ids(program);
    }

    // --- parse_function(): missing ')' after parameter list.
    {
        auto toks = make_tokens({
            {TokenKind::KwFn, "fn"},
            {TokenKind::Identifier, "f"},
            {TokenKind::LParen, "("},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_function());
    }

    // --- while stmt: propagate parse_block() diagnostic.
    {
        auto toks = make_tokens({
            {TokenKind::KwWhile, "while"},
            {TokenKind::LParen, "("},
            {TokenKind::KwTrue, "true"},
            {TokenKind::RParen, ")"},
            {TokenKind::LBrace, "{"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_stmt());
    }

    // --- Duplicate diagnostics with notes.
    {
        auto toks = make_tokens({
            {TokenKind::KwStruct, "struct"},
            {TokenKind::Identifier, "S"},
            {TokenKind::LBrace, "{"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
            {TokenKind::Semicolon, ";"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::Identifier, "Int"},
            {TokenKind::Semicolon, ";"},
            {TokenKind::RBrace, "}"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_struct_decl());
    }

    {
        auto toks = make_tokens({
            {TokenKind::KwEnum, "enum"},
            {TokenKind::Identifier, "E"},
            {TokenKind::LBrace, "{"},
            {TokenKind::Identifier, "V"},
            {TokenKind::Semicolon, ";"},
            {TokenKind::Identifier, "V"},
            {TokenKind::Semicolon, ";"},
            {TokenKind::RBrace, "}"},
        });
        curlee::parser::Parser p(toks);
        expect_diag(p.parse_enum_decl());
    }

    {
        // struct literal duplicate field note
        auto toks = make_tokens({
            {TokenKind::LBrace, "{"},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::IntLiteral, "1"},
            {TokenKind::Comma, ","},
            {TokenKind::Identifier, "x"},
            {TokenKind::Colon, ":"},
            {TokenKind::IntLiteral, "2"},
            {TokenKind::RBrace, "}"},
        });
        curlee::parser::Parser p(toks);
        const auto fake_name = tok(TokenKind::Identifier, "T", 0);
        expect_diag(p.parse_struct_literal_after_name(fake_name));
    }

    return 0;
}
