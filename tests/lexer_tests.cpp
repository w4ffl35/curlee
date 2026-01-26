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

    std::cout << "OK\n";
    return 0;
}
