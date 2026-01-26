#include <curlee/parser/parser.h>

#include <curlee/lexer/token.h>

#include <cassert>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string_view>

namespace curlee::parser {
namespace {

using curlee::lexer::Token;
using curlee::lexer::TokenKind;

static curlee::source::Span span_cover(const curlee::source::Span& a, const curlee::source::Span& b)
{
    return curlee::source::Span{.start = a.start, .end = b.end};
}

class Parser {
public:
    explicit Parser(std::span<const Token> tokens) : tokens_(tokens) {}

    [[nodiscard]] ParseResult parse_program()
    {
        Program program;
        while (!is_at_end()) {
            auto fun = parse_function();
            if (std::holds_alternative<curlee::diag::Diagnostic>(fun)) {
                return std::get<curlee::diag::Diagnostic>(std::move(fun));
            }
            program.functions.push_back(std::get<Function>(std::move(fun)));
        }
        return program;
    }

private:
    std::span<const Token> tokens_;
    std::size_t pos_ = 0;

    [[nodiscard]] bool is_at_end() const { return peek().kind == TokenKind::Eof; }

    [[nodiscard]] const Token& peek() const
    {
        assert(pos_ < tokens_.size());
        return tokens_[pos_];
    }

    [[nodiscard]] const Token& previous() const
    {
        assert(pos_ > 0);
        return tokens_[pos_ - 1];
    }

    [[nodiscard]] bool check(TokenKind kind) const { return peek().kind == kind; }

    const Token& advance()
    {
        if (!is_at_end()) {
            ++pos_;
        }
        return previous();
    }

    bool match(TokenKind kind)
    {
        if (!check(kind)) {
            return false;
        }
        advance();
        return true;
    }

    [[nodiscard]] curlee::diag::Diagnostic error_at(const Token& token, std::string_view message) const
    {
        curlee::diag::Diagnostic d;
        d.severity = curlee::diag::Severity::Error;
        d.message = std::string(message);
        d.span = token.span;
        return d;
    }

    [[nodiscard]] std::optional<curlee::diag::Diagnostic> consume(TokenKind kind, std::string_view message)
    {
        if (check(kind)) {
            advance();
            return std::nullopt;
        }
        return error_at(peek(), message);
    }

    [[nodiscard]] std::variant<Function, curlee::diag::Diagnostic> parse_function()
    {
        if (auto err = consume(TokenKind::KwFn, "expected 'fn'"); err.has_value()) {
            return *err;
        }

        if (!check(TokenKind::Identifier)) {
            return error_at(peek(), "expected function name");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::LParen, "expected '(' after function name"); err.has_value()) {
            return *err;
        }
        if (auto err = consume(TokenKind::RParen, "expected ')' after parameter list"); err.has_value()) {
            return *err;
        }

        std::optional<std::string_view> return_type;
        if (match(TokenKind::Arrow)) {
            if (!check(TokenKind::Identifier)) {
                return error_at(peek(), "expected return type name after '->'");
            }
            return_type = advance().lexeme;
        }

        auto body_res = parse_block();
        if (std::holds_alternative<curlee::diag::Diagnostic>(body_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(body_res));
        }

        Block body = std::get<Block>(std::move(body_res));
        Function fn{
            .span = span_cover(name.span, body.span),
            .name = name.lexeme,
            .body = std::move(body),
            .return_type = return_type,
        };
        return fn;
    }

    [[nodiscard]] std::variant<Block, curlee::diag::Diagnostic> parse_block()
    {
        if (auto err = consume(TokenKind::LBrace, "expected '{' to start block"); err.has_value()) {
            return *err;
        }
        const Token lbrace = previous();

        std::vector<Stmt> stmts;
        while (!check(TokenKind::RBrace) && !is_at_end()) {
            auto stmt_res = parse_stmt();
            if (std::holds_alternative<curlee::diag::Diagnostic>(stmt_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(stmt_res));
            }
            stmts.push_back(std::get<Stmt>(std::move(stmt_res)));
        }

        if (auto err = consume(TokenKind::RBrace, "expected '}' to end block"); err.has_value()) {
            return *err;
        }
        const Token rbrace = previous();

        return Block{.span = span_cover(lbrace.span, rbrace.span), .stmts = std::move(stmts)};
    }

    [[nodiscard]] std::variant<Stmt, curlee::diag::Diagnostic> parse_stmt()
    {
        const std::size_t start_pos = pos_;

        if (match(TokenKind::KwLet)) {
            const Token kw = previous();
            if (!check(TokenKind::Identifier)) {
                return error_at(peek(), "expected identifier after 'let'");
            }
            const Token name = advance();

            if (auto err = consume(TokenKind::Equal, "expected '=' in let statement"); err.has_value()) {
                return *err;
            }

            auto expr_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
            }
            Expr value = std::get<Expr>(std::move(expr_res));

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after let statement"); err.has_value()) {
                return *err;
            }
            const Token semi = previous();

            Stmt stmt{
                .span = span_cover(kw.span, semi.span),
                .node = LetStmt{.name = name.lexeme, .value = std::move(value)},
            };
            return stmt;
        }

        if (match(TokenKind::KwReturn)) {
            const Token kw = previous();
            auto expr_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
            }
            Expr value = std::get<Expr>(std::move(expr_res));

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after return statement"); err.has_value()) {
                return *err;
            }
            const Token semi = previous();

            Stmt stmt{
                .span = span_cover(kw.span, semi.span),
                .node = ReturnStmt{.value = std::move(value)},
            };
            return stmt;
        }

        // Expression statement
        auto expr_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
        }
        Expr expr = std::get<Expr>(std::move(expr_res));

        if (auto err = consume(TokenKind::Semicolon, "expected ';' after expression"); err.has_value()) {
            return *err;
        }
        const Token semi = previous();

        // Span: cover from first token of statement to semicolon.
        const Token first = tokens_[start_pos];
        Stmt stmt{
            .span = span_cover(first.span, semi.span),
            .node = ExprStmt{.expr = std::move(expr)},
        };
        return stmt;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_expr() { return parse_or(); }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_or()
    {
        auto lhs_res = parse_and();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::OrOr)) {
            const Token op = previous();
            auto rhs_res = parse_and();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_and()
    {
        auto lhs_res = parse_equality();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::AndAnd)) {
            const Token op = previous();
            auto rhs_res = parse_equality();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_equality()
    {
        auto lhs_res = parse_comparison();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
            const Token op = previous();
            auto rhs_res = parse_comparison();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_comparison()
    {
        auto lhs_res = parse_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
               match(TokenKind::GreaterEqual)) {
            const Token op = previous();
            auto rhs_res = parse_term();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_term()
    {
        auto lhs_res = parse_factor();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
            const Token op = previous();
            auto rhs_res = parse_factor();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_factor()
    {
        auto lhs_res = parse_unary();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Star) || match(TokenKind::Slash)) {
            const Token op = previous();
            auto rhs_res = parse_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr combined;
            combined.span = span_cover(expr.span, rhs.span);
            combined.node = BinaryExpr{
                .op = op.kind,
                .lhs = std::make_unique<Expr>(std::move(expr)),
                .rhs = std::make_unique<Expr>(std::move(rhs)),
            };
            expr = std::move(combined);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_unary()
    {
        if (match(TokenKind::Bang) || match(TokenKind::Minus)) {
            const Token op = previous();
            auto rhs_res = parse_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            Expr expr;
            expr.span = span_cover(op.span, rhs.span);
            expr.node = UnaryExpr{.op = op.kind, .rhs = std::make_unique<Expr>(std::move(rhs))};
            return expr;
        }

        return parse_call();
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_call()
    {
        auto callee_res = parse_primary();
        if (std::holds_alternative<curlee::diag::Diagnostic>(callee_res)) {
            return std::get<curlee::diag::Diagnostic>(std::move(callee_res));
        }

        Expr expr = std::get<Expr>(std::move(callee_res));

        while (match(TokenKind::LParen)) {
            const Token lparen = previous();

            std::vector<Expr> args;
            if (!check(TokenKind::RParen)) {
                while (true) {
                    auto arg_res = parse_expr();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(arg_res)) {
                        return std::get<curlee::diag::Diagnostic>(std::move(arg_res));
                    }
                    args.push_back(std::get<Expr>(std::move(arg_res)));

                    if (match(TokenKind::Comma)) {
                        continue;
                    }
                    break;
                }
            }

            if (auto err = consume(TokenKind::RParen, "expected ')' after arguments"); err.has_value()) {
                return *err;
            }
            const Token rparen = previous();

            Expr call;
            call.span = span_cover(expr.span, rparen.span);
            call.node = CallExpr{
                .callee = std::make_unique<Expr>(std::move(expr)),
                .args = std::move(args),
            };
            (void)lparen;
            expr = std::move(call);
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_primary()
    {
        if (match(TokenKind::IntLiteral)) {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = IntExpr{.lexeme = lit.lexeme};
            return expr;
        }

        if (match(TokenKind::StringLiteral)) {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = StringExpr{.lexeme = lit.lexeme};
            return expr;
        }

        if (match(TokenKind::Identifier)) {
            const Token name = previous();
            Expr expr;
            expr.span = name.span;
            expr.node = NameExpr{.name = name.lexeme};
            return expr;
        }

        if (match(TokenKind::LParen)) {
            const Token l = previous();
            auto inner_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(inner_res)) {
                return std::get<curlee::diag::Diagnostic>(std::move(inner_res));
            }
            Expr inner = std::get<Expr>(std::move(inner_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after expression"); err.has_value()) {
                return *err;
            }
            const Token r = previous();

            Expr expr;
            expr.span = span_cover(l.span, r.span);
            expr.node = GroupExpr{.inner = std::make_unique<Expr>(std::move(inner))};
            return expr;
        }

        return error_at(peek(), "expected expression");
    }
};

class Dumper {
public:
    explicit Dumper(std::ostringstream& out) : out_(out) {}

    void dump_program(const Program& p)
    {
        for (std::size_t i = 0; i < p.functions.size(); ++i) {
            dump_function(p.functions[i]);
            if (i + 1 < p.functions.size()) {
                out_ << "\n";
            }
        }
    }

private:
    std::ostringstream& out_;

    void dump_function(const Function& f)
    {
        out_ << "fn " << f.name << "()";
        if (f.return_type.has_value()) {
            out_ << " -> " << *f.return_type;
        }
        out_ << " ";
        dump_block(f.body);
    }

    void dump_block(const Block& b)
    {
        out_ << "{";
        for (const auto& s : b.stmts) {
            out_ << " ";
            dump_stmt(s);
        }
        out_ << " }";
    }

    void dump_stmt(const Stmt& s)
    {
        std::visit([&](const auto& node) { dump_stmt_node(node); }, s.node);
    }

    void dump_stmt_node(const LetStmt& s)
    {
        out_ << "let " << s.name << " = ";
        dump_expr(s.value);
        out_ << ";";
    }

    void dump_stmt_node(const ReturnStmt& s)
    {
        out_ << "return ";
        dump_expr(s.value);
        out_ << ";";
    }

    void dump_stmt_node(const ExprStmt& s)
    {
        dump_expr(s.expr);
        out_ << ";";
    }

    void dump_expr(const Expr& e)
    {
        std::visit([&](const auto& node) { dump_expr_node(node); }, e.node);
    }

    void dump_expr_node(const IntExpr& e) { out_ << e.lexeme; }
    void dump_expr_node(const StringExpr& e) { out_ << e.lexeme; }
    void dump_expr_node(const NameExpr& e) { out_ << e.name; }

    void dump_expr_node(const GroupExpr& e)
    {
        out_ << "(";
        dump_expr(*e.inner);
        out_ << ")";
    }

    void dump_expr_node(const UnaryExpr& e)
    {
        out_ << curlee::lexer::to_string(e.op) << " ";
        dump_expr(*e.rhs);
    }

    void dump_expr_node(const BinaryExpr& e)
    {
        out_ << "(";
        dump_expr(*e.lhs);
        out_ << " " << curlee::lexer::to_string(e.op) << " ";
        dump_expr(*e.rhs);
        out_ << ")";
    }

    void dump_expr_node(const CallExpr& e)
    {
        dump_expr(*e.callee);
        out_ << "(";
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            dump_expr(e.args[i]);
            if (i + 1 < e.args.size()) {
                out_ << ", ";
            }
        }
        out_ << ")";
    }
};

} // namespace

ParseResult parse(std::span<const curlee::lexer::Token> tokens)
{
    return Parser(tokens).parse_program();
}

std::string dump(const Program& program)
{
    std::ostringstream out;
    Dumper d(out);
    d.dump_program(program);
    return out.str();
}

} // namespace curlee::parser
