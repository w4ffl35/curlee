#include <cstdlib>
#include <curlee/lexer/lexer.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static void expect_token(const std::vector<curlee::lexer::Token>& tokens, std::size_t index,
                         curlee::lexer::TokenKind kind, std::string_view lexeme)
{
    if (index >= tokens.size())
    {
        fail("missing token at index " + std::to_string(index));
    }

    const auto& t = tokens[index];
    if (t.kind != kind)
    {
        fail("token kind mismatch at index " + std::to_string(index));
    }
    if (t.lexeme != lexeme)
    {
        fail("token lexeme mismatch at index " + std::to_string(index));
    }
}

int main()
{
    using namespace curlee::lexer;

    // Cover TokenKind stringification (used in diagnostics and tests).
    {
        constexpr TokenKind all_kinds[] = {
            TokenKind::Eof,           TokenKind::Identifier,   TokenKind::IntLiteral,
            TokenKind::StringLiteral, TokenKind::KwFn,         TokenKind::KwLet,
            TokenKind::KwIf,          TokenKind::KwElse,       TokenKind::KwWhile,
            TokenKind::KwReturn,      TokenKind::KwTrue,       TokenKind::KwFalse,
            TokenKind::KwRequires,    TokenKind::KwEnsures,    TokenKind::KwWhere,
            TokenKind::KwUnsafe,      TokenKind::KwCap,        TokenKind::KwImport,
            TokenKind::KwAs,          TokenKind::KwStruct,     TokenKind::KwEnum,
            TokenKind::LParen,        TokenKind::RParen,       TokenKind::LBrace,
            TokenKind::RBrace,        TokenKind::LBracket,     TokenKind::RBracket,
            TokenKind::Semicolon,     TokenKind::Comma,        TokenKind::Colon,
            TokenKind::ColonColon,    TokenKind::Dot,          TokenKind::Arrow,
            TokenKind::Equal,         TokenKind::EqualEqual,   TokenKind::Bang,
            TokenKind::BangEqual,     TokenKind::Less,         TokenKind::LessEqual,
            TokenKind::Greater,       TokenKind::GreaterEqual, TokenKind::Plus,
            TokenKind::Minus,         TokenKind::Star,         TokenKind::Slash,
            TokenKind::AndAnd,        TokenKind::OrOr,
        };

        for (const auto kind : all_kinds)
        {
            if (to_string(kind) == "unknown")
            {
                fail("expected TokenKind to stringify to a stable name");
            }
        }

        if (to_string(static_cast<TokenKind>(-1)) != "unknown")
        {
            fail("expected invalid TokenKind to stringify to 'unknown'");
        }
    }

    {
        const std::string src = "fn f() { return x; }";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for simple function");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwFn, "fn");
        expect_token(toks, 1, TokenKind::Identifier, "f");
        expect_token(toks, 2, TokenKind::LParen, "(");
        expect_token(toks, 3, TokenKind::RParen, ")");
        expect_token(toks, 4, TokenKind::LBrace, "{");
        expect_token(toks, 5, TokenKind::KwReturn, "return");
        expect_token(toks, 6, TokenKind::Identifier, "x");
        expect_token(toks, 7, TokenKind::Semicolon, ";");
        expect_token(toks, 8, TokenKind::RBrace, "}");
        expect_token(toks, 9, TokenKind::Eof, "");
    }

    {
        const std::string src = "if x { } else { }";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for if/else");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwIf, "if");
        expect_token(toks, 1, TokenKind::Identifier, "x");
        expect_token(toks, 2, TokenKind::LBrace, "{");
        expect_token(toks, 3, TokenKind::RBrace, "}");
        expect_token(toks, 4, TokenKind::KwElse, "else");
        expect_token(toks, 5, TokenKind::LBrace, "{");
        expect_token(toks, 6, TokenKind::RBrace, "}");
        expect_token(toks, 7, TokenKind::Eof, "");
    }

    {
        const std::string src = "requires x > 0; // comment\nensures x >= 1;";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for requires/ensures");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwRequires, "requires");
        expect_token(toks, 1, TokenKind::Identifier, "x");
        expect_token(toks, 2, TokenKind::Greater, ">");
        expect_token(toks, 3, TokenKind::IntLiteral, "0");
        expect_token(toks, 4, TokenKind::Semicolon, ";");
        expect_token(toks, 5, TokenKind::KwEnsures, "ensures");
        expect_token(toks, 6, TokenKind::Identifier, "x");
        expect_token(toks, 7, TokenKind::GreaterEqual, ">=");
        expect_token(toks, 8, TokenKind::IntLiteral, "1");
        expect_token(toks, 9, TokenKind::Semicolon, ";");
        expect_token(toks, 10, TokenKind::Eof, "");
    }

    {
        const std::string src = "let b = true; let c = false;";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for boolean literals");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwLet, "let");
        expect_token(toks, 1, TokenKind::Identifier, "b");
        expect_token(toks, 2, TokenKind::Equal, "=");
        expect_token(toks, 3, TokenKind::KwTrue, "true");
        expect_token(toks, 4, TokenKind::Semicolon, ";");
        expect_token(toks, 5, TokenKind::KwLet, "let");
        expect_token(toks, 6, TokenKind::Identifier, "c");
        expect_token(toks, 7, TokenKind::Equal, "=");
        expect_token(toks, 8, TokenKind::KwFalse, "false");
        expect_token(toks, 9, TokenKind::Semicolon, ";");
        expect_token(toks, 10, TokenKind::Eof, "");
    }

    {
        const std::string src = "import foo.bar;";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for import path");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwImport, "import");
        expect_token(toks, 1, TokenKind::Identifier, "foo");
        expect_token(toks, 2, TokenKind::Dot, ".");
        expect_token(toks, 3, TokenKind::Identifier, "bar");
        expect_token(toks, 4, TokenKind::Semicolon, ";");
        expect_token(toks, 5, TokenKind::Eof, "");
    }

    {
        const std::string src = "/* unterminated";
        const auto res = lex(src);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
        {
            fail("expected error for unterminated block comment");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(res);
        if (d.message != "unterminated block comment")
        {
            fail("unexpected diagnostic message");
        }
        if (!d.span.has_value())
        {
            fail("expected span for diagnostic");
        }
    }

    {
        const std::string src = "let s = \"hi\\n\\\"there\";";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for string literal");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwLet, "let");
        expect_token(toks, 1, TokenKind::Identifier, "s");
        expect_token(toks, 2, TokenKind::Equal, "=");
        expect_token(toks, 3, TokenKind::StringLiteral, "\"hi\\n\\\"there\"");
        expect_token(toks, 4, TokenKind::Semicolon, ";");
        expect_token(toks, 5, TokenKind::Eof, "");
    }

    {
        const std::string src = "@";
        const auto res = lex(src);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
        {
            fail("expected error for invalid character");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(res);
        if (d.message != "invalid character")
        {
            fail("unexpected diagnostic message for invalid character");
        }
        if (!d.span.has_value() || d.span->start != 0 || d.span->end != 1)
        {
            fail("unexpected span for invalid character");
        }
    }

    {
        const std::string src = "\"unterminated";
        const auto res = lex(src);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
        {
            fail("expected error for unterminated string literal");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(res);
        if (d.message != "unterminated string literal")
        {
            fail("unexpected diagnostic message for unterminated string literal");
        }
        if (!d.span.has_value())
        {
            fail("expected span for unterminated string literal");
        }
    }

    // Unterminated string due to newline.
    {
        const std::string src = "\"hi\nthere\"";
        const auto res = lex(src);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
        {
            fail("expected error for newline in string literal");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(res);
        if (d.message != "unterminated string literal")
        {
            fail("unexpected diagnostic message for newline in string literal");
        }
        if (!d.span.has_value() || d.span->start != 0 || d.span->end != 3)
        {
            fail("unexpected span for newline in string literal");
        }
    }

    // Unterminated string due to trailing backslash at EOF.
    {
        const std::string src = "\"hi\\";
        const auto res = lex(src);
        if (!std::holds_alternative<curlee::diag::Diagnostic>(res))
        {
            fail("expected error for trailing backslash in string literal");
        }

        const auto& d = std::get<curlee::diag::Diagnostic>(res);
        if (d.message != "unterminated string literal")
        {
            fail("unexpected diagnostic message for trailing backslash");
        }
        if (!d.span.has_value() || d.span->start != 0 || d.span->end != 4)
        {
            fail("unexpected span for trailing backslash");
        }
    }

    // Exercise: underscore identifier start, multi-character identifiers, and multi-digit ints.
    {
        const std::string src = "let _abc123 = 12345;";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for underscore identifier and multi-digit int");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwLet, "let");
        expect_token(toks, 1, TokenKind::Identifier, "_abc123");
        expect_token(toks, 2, TokenKind::Equal, "=");
        expect_token(toks, 3, TokenKind::IntLiteral, "12345");
        expect_token(toks, 4, TokenKind::Semicolon, ";");
        expect_token(toks, 5, TokenKind::Eof, "");
    }

    // Exercise: keyword cap.
    {
        const std::string src = "cap foo;";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for cap keyword");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwCap, "cap");
        expect_token(toks, 1, TokenKind::Identifier, "foo");
        expect_token(toks, 2, TokenKind::Semicolon, ";");
        expect_token(toks, 3, TokenKind::Eof, "");
    }

    // Exercise: two-character operators (<=, >=, ==, !=, &&, ||) and trivia skipping.
    {
        const std::string src =
            " \t// line comment\n"
            "/* block */ if x <= 1 && x >= 0 || x == 2 || x != 3 { }";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for operators and comments");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::KwIf, "if");
        expect_token(toks, 1, TokenKind::Identifier, "x");
        expect_token(toks, 2, TokenKind::LessEqual, "<=");
        expect_token(toks, 3, TokenKind::IntLiteral, "1");
        expect_token(toks, 4, TokenKind::AndAnd, "&&");
        expect_token(toks, 5, TokenKind::Identifier, "x");
        expect_token(toks, 6, TokenKind::GreaterEqual, ">=");
        expect_token(toks, 7, TokenKind::IntLiteral, "0");
        expect_token(toks, 8, TokenKind::OrOr, "||");
        expect_token(toks, 9, TokenKind::Identifier, "x");
        expect_token(toks, 10, TokenKind::EqualEqual, "==");
        expect_token(toks, 11, TokenKind::IntLiteral, "2");
        expect_token(toks, 12, TokenKind::OrOr, "||");
        expect_token(toks, 13, TokenKind::Identifier, "x");
        expect_token(toks, 14, TokenKind::BangEqual, "!=");
        expect_token(toks, 15, TokenKind::IntLiteral, "3");
        expect_token(toks, 16, TokenKind::LBrace, "{");
        expect_token(toks, 17, TokenKind::RBrace, "}");
        expect_token(toks, 18, TokenKind::Eof, "");
    }

    // Exercise: :: token.
    {
        const std::string src = "foo::bar";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for :: token");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::Identifier, "foo");
        expect_token(toks, 1, TokenKind::ColonColon, "::");
        expect_token(toks, 2, TokenKind::Identifier, "bar");
        expect_token(toks, 3, TokenKind::Eof, "");
    }

    // Exercise: line comment that ends at EOF (no trailing newline).
    {
        const std::string src = "// comment";
        const auto res = lex(src);
        if (!std::holds_alternative<std::vector<Token>>(res))
        {
            fail("expected success for EOF line comment");
        }

        const auto& toks = std::get<std::vector<Token>>(res);
        expect_token(toks, 0, TokenKind::Eof, "");
    }

    std::cout << "OK\n";
    return 0;
}
