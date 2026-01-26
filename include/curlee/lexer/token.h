#pragma once

#include <curlee/source/span.h>
#include <string_view>

namespace curlee::lexer
{

enum class TokenKind
{
    Eof,

    Identifier,
    IntLiteral,
    StringLiteral,

    // Keywords
    KwFn,
    KwLet,
    KwIf,
    KwWhile,
    KwReturn,

    KwRequires,
    KwEnsures,
    KwWhere,

    KwUnsafe,
    KwCap,
    KwImport,

    // Punctuation / operators
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,

    Semicolon,
    Comma,
    Colon,

    Arrow,      // ->
    Equal,      // =
    EqualEqual, // ==
    Bang,       // !
    BangEqual,  // !=

    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    Plus,
    Minus,
    Star,
    Slash,

    AndAnd,
    OrOr,
};

struct Token
{
    TokenKind kind = TokenKind::Eof;
    std::string_view lexeme;
    curlee::source::Span span;
};

[[nodiscard]] constexpr std::string_view to_string(TokenKind kind)
{
    switch (kind)
    {
    case TokenKind::Eof:
        return "eof";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::IntLiteral:
        return "int";
    case TokenKind::StringLiteral:
        return "string";

    case TokenKind::KwFn:
        return "kw_fn";
    case TokenKind::KwLet:
        return "kw_let";
    case TokenKind::KwIf:
        return "kw_if";
    case TokenKind::KwWhile:
        return "kw_while";
    case TokenKind::KwReturn:
        return "kw_return";

    case TokenKind::KwRequires:
        return "kw_requires";
    case TokenKind::KwEnsures:
        return "kw_ensures";
    case TokenKind::KwWhere:
        return "kw_where";

    case TokenKind::KwUnsafe:
        return "kw_unsafe";
    case TokenKind::KwCap:
        return "kw_cap";
    case TokenKind::KwImport:
        return "kw_import";

    case TokenKind::LParen:
        return "l_paren";
    case TokenKind::RParen:
        return "r_paren";
    case TokenKind::LBrace:
        return "l_brace";
    case TokenKind::RBrace:
        return "r_brace";
    case TokenKind::LBracket:
        return "l_bracket";
    case TokenKind::RBracket:
        return "r_bracket";

    case TokenKind::Semicolon:
        return "semicolon";
    case TokenKind::Comma:
        return "comma";
    case TokenKind::Colon:
        return "colon";

    case TokenKind::Arrow:
        return "arrow";
    case TokenKind::Equal:
        return "equal";
    case TokenKind::EqualEqual:
        return "equal_equal";
    case TokenKind::Bang:
        return "bang";
    case TokenKind::BangEqual:
        return "bang_equal";

    case TokenKind::Less:
        return "less";
    case TokenKind::LessEqual:
        return "less_equal";
    case TokenKind::Greater:
        return "greater";
    case TokenKind::GreaterEqual:
        return "greater_equal";

    case TokenKind::Plus:
        return "plus";
    case TokenKind::Minus:
        return "minus";
    case TokenKind::Star:
        return "star";
    case TokenKind::Slash:
        return "slash";

    case TokenKind::AndAnd:
        return "and_and";
    case TokenKind::OrOr:
        return "or_or";
    }
    return "unknown";
}

} // namespace curlee::lexer
