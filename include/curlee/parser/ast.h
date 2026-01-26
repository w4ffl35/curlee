#pragma once

#include <curlee/lexer/token.h>
#include <curlee/source/span.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace curlee::parser {

struct Expr;

struct IntExpr {
    std::string_view lexeme;
};

struct StringExpr {
    std::string_view lexeme; // includes quotes, preserves escapes
};

struct NameExpr {
    std::string_view name;
};

struct UnaryExpr {
    curlee::lexer::TokenKind op;
    std::unique_ptr<Expr> rhs;
};

struct BinaryExpr {
    curlee::lexer::TokenKind op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct CallExpr {
    std::unique_ptr<Expr> callee;
    std::vector<Expr> args;
};

struct GroupExpr {
    std::unique_ptr<Expr> inner;
};

struct Expr {
    curlee::source::Span span;
    std::variant<IntExpr, StringExpr, NameExpr, UnaryExpr, BinaryExpr, CallExpr, GroupExpr> node;
};

struct LetStmt {
    std::string_view name;
    Expr value;
};

struct ReturnStmt {
    Expr value;
};

struct ExprStmt {
    Expr expr;
};

struct Stmt {
    curlee::source::Span span;
    std::variant<LetStmt, ReturnStmt, ExprStmt> node;
};

struct Block {
    curlee::source::Span span;
    std::vector<Stmt> stmts;
};

struct Function {
    curlee::source::Span span;
    std::string_view name;
    Block body;

    // Optional return type (MVP: identifier only). Present when `->` appears.
    std::optional<std::string_view> return_type;
};

struct Program {
    std::vector<Function> functions;
};

} // namespace curlee::parser
