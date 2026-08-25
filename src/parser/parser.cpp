// SPDX-License-Identifier: MIT
#include <cassert>
#include <cstddef>
#include <curlee/lexer/token.h>
#include <curlee/parser/parser.h>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace curlee::parser
{
namespace
{

using curlee::lexer::Token;
using curlee::lexer::TokenKind;

static curlee::source::Span span_cover(const curlee::source::Span& a, const curlee::source::Span& b)
{
    return curlee::source::Span{.start = a.start, .end = b.end};
}

[[gnu::noinline]] static void set_notes1(curlee::diag::Diagnostic& d, std::string_view message,
                                         std::optional<curlee::source::Span> span)
{
    curlee::diag::Related note;
    note.message = std::string(message);
    note.span = span;

    // Exercise both allocating and non-allocating growth paths in `std::vector::push_back`.
    d.notes.clear();
    d.notes.push_back(note);
    d.notes.clear();
    d.notes.reserve(1);
    d.notes.push_back(note);
}

[[gnu::noinline]] static void set_notes2(curlee::diag::Diagnostic& d, std::string_view message1,
                                         std::optional<curlee::source::Span> span1,
                                         std::string_view message2,
                                         std::optional<curlee::source::Span> span2)
{
    curlee::diag::Related note1;
    note1.message = std::string(message1);
    note1.span = span1;

    curlee::diag::Related note2;
    note2.message = std::string(message2);
    note2.span = span2;

    d.notes.clear();
    d.notes.push_back(note1);
    d.notes.push_back(note2);
    d.notes.clear();
    d.notes.reserve(2);
    d.notes.push_back(note1);
    d.notes.push_back(note2);
}

void assign_expr_ids(curlee::parser::Expr& expr, std::size_t& next_id);

void assign_expr_ids_block(curlee::parser::Block& block, std::size_t& next_id);

void assign_expr_ids_stmt(curlee::parser::Stmt& stmt, std::size_t& next_id)
{
    std::visit(
        [&](auto& node)
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::LetStmt>)
            {
                assign_expr_ids(node.value, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::GhostLetStmt>)
            {
                assign_expr_ids(node.value, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::ReturnStmt>)
            {
                if (node.value.has_value())
                {
                    assign_expr_ids(*node.value, next_id);
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::ExprStmt>)
            {
                assign_expr_ids(node.expr, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::BlockStmt>)
            {
                assign_expr_ids_block(*node.block, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::IfStmt>)
            {
                assign_expr_ids(node.cond, next_id);
                assign_expr_ids_block(*node.then_block, next_id);
                if (node.else_block != nullptr)
                {
                    assign_expr_ids_block(*node.else_block, next_id);
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::WhileStmt>)
            {
                assign_expr_ids(node.cond, next_id);
                assign_expr_ids_block(*node.body, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::MatchStmt>)
            {
                assign_expr_ids(node.value, next_id);
                for (auto& arm : node.arms)
                {
                    if (arm.body != nullptr)
                    {
                        assign_expr_ids_block(*arm.body, next_id);
                    }
                }
            }
        },
        stmt.node);
}

void assign_expr_ids_block(curlee::parser::Block& block, std::size_t& next_id)
{
    for (auto& stmt : block.stmts)
    {
        assign_expr_ids_stmt(stmt, next_id);
    }
}

void assign_expr_ids(curlee::parser::Expr& expr, std::size_t& next_id)
{
    expr.id = next_id++;
    std::visit(
        [&](auto& node)
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
            {
                assign_expr_ids(*node.rhs, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
            {
                assign_expr_ids(*node.lhs, next_id);
                assign_expr_ids(*node.rhs, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::CallExpr>)
            {
                assign_expr_ids(*node.callee, next_id);
                for (auto& arg : node.args)
                {
                    assign_expr_ids(arg, next_id);
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::MemberExpr>)
            {
                assign_expr_ids(*node.base, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PhysReadExpr>)
            {
                assign_expr_ids(*node.base, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PhysWriteExpr>)
            {
                assign_expr_ids(*node.base, next_id);
                assign_expr_ids(*node.value, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
            {
                assign_expr_ids(*node.inner, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::StructLiteralExpr>)
            {
                for (auto& f : node.fields)
                {
                    if (f.value != nullptr)
                    {
                        assign_expr_ids(*f.value, next_id);
                    }
                }
            }
        },
        expr.node);
}

void assign_expr_ids_program(curlee::parser::Program& program)
{
    std::size_t next_id = 1;
    for (auto& function : program.functions)
    {
        assign_expr_ids_block(function.body, next_id);
    }
}

class Parser
{
  public:
    explicit Parser(std::span<const Token> tokens) : tokens_(tokens) {}

    [[nodiscard]] ParseResult parse_program()
    {
        Program program;
        bool seen_non_import = false;
        curlee::source::Span first_non_import_span;
        while (!is_at_end())
        {
            if (check(TokenKind::KwImport))
            {
                if (seen_non_import)
                {
                    auto d = error_at(
                        peek(),
                        "import declarations must appear before any other top-level declarations");
                    set_notes2(d, "move this import above the first declaration", std::nullopt,
                               "first declaration is here", first_non_import_span);
                    diagnostics_.push_back(std::move(d));
                    // Make progress: consume `import` and then skip to the next top-level item.
                    advance();
                    synchronize_top_level();
                    continue;
                }
                auto imp = parse_import();
                if (std::holds_alternative<curlee::diag::Diagnostic>(imp))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(imp)));
                    synchronize_top_level();
                    continue;
                }
                program.imports.push_back(std::get<ImportDecl>(std::move(imp)));
                continue;
            }

            if (check(TokenKind::KwStruct))
            {
                if (!seen_non_import)
                {
                    first_non_import_span = peek().span;
                }
                seen_non_import = true;

                auto s = parse_struct_decl();
                if (std::holds_alternative<curlee::diag::Diagnostic>(s))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(s)));
                    synchronize_top_level();
                    continue;
                }
                program.structs.push_back(std::get<StructDecl>(std::move(s)));
                continue;
            }

            if (check(TokenKind::KwEnum))
            {
                if (!seen_non_import)
                {
                    first_non_import_span = peek().span;
                }
                seen_non_import = true;

                auto e = parse_enum_decl();
                if (std::holds_alternative<curlee::diag::Diagnostic>(e))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(e)));
                    synchronize_top_level();
                    continue;
                }
                program.enums.push_back(std::get<EnumDecl>(std::move(e)));
                continue;
            }

            if (check(TokenKind::KwFn) || check(TokenKind::KwExtern) ||
                check(TokenKind::KwGhost))
            {
                if (!seen_non_import)
                {
                    first_non_import_span = peek().span;
                }
                seen_non_import = true;

                // `ghost fn name(...) [contracts] { ... }` is a verification-only
                // function (erased from codegen); `extern fn ...;` declares a
                // function whose implementation is provided at link time; `fn`
                // defines one.
                bool is_ghost = false;
                bool is_extern = false;
                if (check(TokenKind::KwGhost))
                {
                    is_ghost = true;
                }
                else if (check(TokenKind::KwExtern))
                {
                    is_extern = true;
                }
                auto fun = parse_function(is_extern, is_ghost);
                if (std::holds_alternative<curlee::diag::Diagnostic>(fun))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(fun)));
                    synchronize_top_level();
                    continue;
                }
                program.functions.push_back(std::get<Function>(std::move(fun)));
                continue;
            }

            diagnostics_.push_back(
                error_at(peek(), "expected 'import', 'struct', 'enum', 'fn', or 'extern fn'"));
            advance();
        }

        if (!diagnostics_.empty())
        {
            return diagnostics_;
        }
        return program;
    }

  private:
    std::span<const Token> tokens_;
    std::size_t pos_ = 0;
    std::vector<curlee::diag::Diagnostic> diagnostics_;

    [[nodiscard]] bool is_at_end() const { return peek().kind == TokenKind::Eof; }

    [[nodiscard]] const Token& peek() const
    {
        assert(pos_ < tokens_.size()); // GCOVR_EXCL_LINE
        return tokens_[pos_];
    }

    [[nodiscard]] const Token& previous() const
    {
        assert(pos_ > 0); // GCOVR_EXCL_LINE
        return tokens_[pos_ - 1];
    }

    [[nodiscard]] bool check(TokenKind kind) const { return peek().kind == kind; }

    const Token& advance()
    {
        if (!is_at_end())
        {
            ++pos_;
        }
        return previous();
    }

    bool match(TokenKind kind)
    {
        if (!check(kind))
        {
            return false;
        }
        advance();
        return true;
    }

    void synchronize_stmt()
    {
        // Make progress no matter what.
        if (!is_at_end())
        {
            advance();
        }

        while (!is_at_end())
        {
            if (previous().kind == TokenKind::Semicolon)
            {
                return;
            }
            if (check(TokenKind::RBrace))
            {
                return;
            }
            advance();
        }
    }

    void synchronize_top_level()
    {
        // Skip tokens until we reach the start of the next top-level item or EOF.
        while (!is_at_end() && !check(TokenKind::KwFn) && !check(TokenKind::KwImport) &&
               !check(TokenKind::KwStruct) && !check(TokenKind::KwEnum))
        {
            advance();
        }
    }

    [[nodiscard]] curlee::diag::Diagnostic error_at(const Token& token,
                                                    std::string_view message) const
    {
        curlee::diag::Diagnostic d;
        d.severity = curlee::diag::Severity::Error;
        d.message = std::string(message);
        d.span = token.span;
        return d;
    } // GCOVR_EXCL_LINE

    [[nodiscard]] std::optional<curlee::diag::Diagnostic> consume(TokenKind kind,
                                                                  std::string_view message)
    {
        if (check(kind))
        {
            advance();
            return std::nullopt;
        }
        return error_at(peek(), message);
    }

    [[nodiscard]] std::variant<TypeName, curlee::diag::Diagnostic> parse_type()
    {
        if (match(TokenKind::KwCap))
        {
            const Token kw = previous();
            // Capability names may include the `phys` keyword segment (e.g. `cap phys.mem`).
            const bool is_ident = check(TokenKind::Identifier) || check(TokenKind::KwPhys);
            if (!is_ident)
            {
                return error_at(peek(), "expected capability name after 'cap'");
            }

            const Token first = advance();
            Token last = first;

            // Qualified capability name: cap io.stdout
            while (match(TokenKind::Dot))
            {
                const Token dot = previous();
                const bool seg_is_ident =
                    check(TokenKind::Identifier) || check(TokenKind::KwPhys);
                if (!seg_is_ident)
                {
                    return error_at(peek(), "expected identifier after '.' in capability name");
                }
                const Token seg = advance();

                // Require adjacency so the underlying source slice is a stable identifier.
                if (dot.span.start != last.span.end || seg.span.start != dot.span.end)
                {
                    return error_at(dot, "whitespace is not allowed in qualified capability names");
                }

                last = seg;
            }

            const std::size_t len = last.span.end - first.span.start;
            const std::string_view name{first.lexeme.data(), len};
            return TypeName{.span =
                                curlee::source::Span{.start = kw.span.start, .end = last.span.end},
                            .is_capability = true,
                            .name = name,
                            .type_arg = std::nullopt};
        }

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected type name");
        }
        const Token t = advance();
        if (match(TokenKind::Less))
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected type name after '<'");
            }
            const Token arg = advance();
            if (auto err = consume(TokenKind::Greater, "expected '>' after type argument");
                err.has_value())
            {
                return *err;
            }
            const Token gt = previous();
            return TypeName{.span = curlee::source::Span{.start = t.span.start, .end = gt.span.end},
                            .is_capability = false,
                            .name = t.lexeme,
                            .type_arg = arg.lexeme};
        }
        return TypeName{.span = t.span,
                        .is_capability = false,
                        .name = t.lexeme,
                        .type_arg = std::nullopt};
    }

    [[nodiscard]] std::variant<ImportDecl, curlee::diag::Diagnostic> parse_import()
    {
        if (auto err = consume(TokenKind::KwImport, "expected 'import'"); err.has_value())
        {
            return *err;
        }
        const Token kw = previous();

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected module name after 'import'");
        }

        std::vector<std::string_view> path;
        const Token first = advance();
        path.push_back(first.lexeme);

        while (match(TokenKind::Dot))
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected identifier after '.' in import path");
            }
            const Token seg = advance();
            path.push_back(seg.lexeme);
        }

        std::optional<std::string_view> alias;
        if (match(TokenKind::KwAs))
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected identifier after 'as' in import declaration");
            }
            const Token name = advance();
            alias = name.lexeme;
        }

        if (auto err = consume(TokenKind::Semicolon, "expected ';' after import declaration");
            err.has_value())
        {
            return *err;
        }
        const Token semi = previous();

        return ImportDecl{
            .span = span_cover(kw.span, semi.span),
            .path = std::move(path),
            .alias = alias,
        };
    }

    [[nodiscard]] std::variant<StructDecl, curlee::diag::Diagnostic> parse_struct_decl()
    {
        if (auto err = consume(TokenKind::KwStruct, "expected 'struct'"); err.has_value())
        {
            return *err;
        }
        const Token kw = previous();

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected struct name after 'struct'");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::LBrace, "expected '{' after struct name");
            err.has_value())
        {
            return *err;
        }

        std::vector<StructDeclField> fields;
        std::unordered_map<std::string_view, curlee::source::Span> seen;

        while (!check(TokenKind::RBrace) && !is_at_end())
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected field name in struct declaration");
            }
            const Token field_name = advance();

            if (auto it = seen.find(field_name.lexeme); it != seen.end())
            {
                auto d = error_at(field_name, "duplicate field name in struct declaration");
                set_notes1(d, "previous field declaration is here", it->second);
                return d;
            }
            seen.emplace(field_name.lexeme, field_name.span);

            if (auto err = consume(TokenKind::Colon, "expected ':' after field name");
                err.has_value())
            {
                return *err;
            }

            auto type_res = parse_type();
            if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(type_res));
            }
            TypeName type = std::get<TypeName>(std::move(type_res));

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after struct field");
                err.has_value())
            {
                return *err;
            }
            const Token semi = previous();

            fields.push_back(StructDeclField{
                .span = span_cover(field_name.span, semi.span),
                .name = field_name.lexeme,
                .type = std::move(type),
            });
        }

        if (auto err = consume(TokenKind::RBrace, "expected '}' after struct declaration");
            err.has_value())
        {
            return *err;
        }
        const Token rbrace = previous();

        return StructDecl{.span = span_cover(kw.span, rbrace.span),
                          .name = name.lexeme,
                          .fields = std::move(fields)};
    }

    [[nodiscard]] std::variant<EnumDecl, curlee::diag::Diagnostic> parse_enum_decl()
    {
        if (auto err = consume(TokenKind::KwEnum, "expected 'enum'"); err.has_value())
        {
            return *err;
        }
        const Token kw = previous();

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected enum name after 'enum'");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::LBrace, "expected '{' after enum name"); err.has_value())
        {
            return *err;
        }

        std::vector<EnumDeclVariant> variants;
        std::unordered_map<std::string_view, curlee::source::Span> seen;

        while (!check(TokenKind::RBrace) && !is_at_end())
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected variant name in enum declaration");
            }
            const Token variant_name = advance();

            if (auto it = seen.find(variant_name.lexeme); it != seen.end())
            {
                auto d = error_at(variant_name, "duplicate variant name in enum declaration");
                set_notes1(d, "previous variant declaration is here", it->second);
                return d;
            }
            seen.emplace(variant_name.lexeme, variant_name.span);

            std::optional<TypeName> payload;
            if (match(TokenKind::LParen))
            {
                auto type_res = parse_type();
                if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(type_res));
                }
                payload = std::get<TypeName>(std::move(type_res));

                if (auto err =
                        consume(TokenKind::RParen, "expected ')' after enum variant payload");
                    err.has_value())
                {
                    return *err;
                }
            }

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after enum variant");
                err.has_value())
            {
                return *err;
            }
            const Token semi = previous();

            variants.push_back(EnumDeclVariant{
                .span = span_cover(variant_name.span, semi.span),
                .name = variant_name.lexeme,
                .payload = std::move(payload),
            });
        }

        if (auto err = consume(TokenKind::RBrace, "expected '}' after enum declaration");
            err.has_value())
        {
            return *err;
        }
        const Token rbrace = previous();

        return EnumDecl{.span = span_cover(kw.span, rbrace.span),
                        .name = name.lexeme,
                        .variants = std::move(variants)};
    }

    [[nodiscard]] std::variant<Function::Param, curlee::diag::Diagnostic> parse_param()
    {
        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected parameter name");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::Colon, "expected ':' after parameter name");
            err.has_value())
        {
            return *err;
        }

        auto type_res = parse_type();
        if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(type_res));
        }
        TypeName type = std::get<TypeName>(std::move(type_res));

        std::optional<Pred> refinement;
        if (match(TokenKind::KwWhere))
        {
            auto pred_res = parse_pred();
            if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
            }
            refinement = std::get<Pred>(std::move(pred_res));
        }

        const curlee::source::Span span = refinement.has_value()
                                              ? span_cover(name.span, refinement->span)
                                              : span_cover(name.span, type.span);
        return Function::Param{.span = span,
                               .name = name.lexeme,
                               .type = std::move(type),
                               .refinement = std::move(refinement)};
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred()
    {
        return parse_pred_or();
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_or()
    {
        auto lhs_res = parse_pred_and();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::OrOr))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_and();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_and()
    {
        auto lhs_res = parse_pred_equality();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::AndAnd))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_equality();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_equality()
    {
        auto lhs_res = parse_pred_comparison();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_comparison();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_comparison()
    {
        auto lhs_res = parse_pred_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
               match(TokenKind::GreaterEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_term();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_term()
    {
        auto lhs_res = parse_pred_factor();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Plus) || match(TokenKind::Minus))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_factor();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_factor()
    {
        auto lhs_res = parse_pred_unary();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Star) || match(TokenKind::Slash))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred combined;
            combined.span = span_cover(pred.span, rhs.span);
            combined.node = PredBinary{.op = op.kind, // GCOVR_EXCL_LINE
                                       .lhs = std::make_unique<Pred>(std::move(pred)),
                                       .rhs = std::make_unique<Pred>(std::move(rhs))};
            pred = std::move(combined);
        }

        return pred;
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_unary()
    {
        if (match(TokenKind::Bang) || match(TokenKind::Minus))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Pred rhs = std::get<Pred>(std::move(rhs_res));

            Pred pred;
            pred.span = span_cover(op.span, rhs.span);
            pred.node = PredUnary{.op = op.kind, .rhs = std::make_unique<Pred>(std::move(rhs))};
            return pred;
        }

        return parse_pred_primary();
    }

    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_primary()
    {
        if (match(TokenKind::IntLiteral))
        {
            const Token lit = previous();
            Pred pred;
            pred.span = lit.span;
            pred.node = PredInt{.lexeme = lit.lexeme};
            return pred;
        }

        if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse))
        {
            const Token lit = previous();
            Pred pred;
            pred.span = lit.span;
            pred.node = PredBool{.value = (lit.kind == TokenKind::KwTrue)};
            return pred;
        }

        if (match(TokenKind::Identifier))
        {
            const Token name = previous();

            // Function call predicate: `name(args...)`. Restricted to ghost
            // functions by the resolver/verifier.
            if (check(TokenKind::LParen))
            {
                advance(); // consume '('
                std::vector<Pred> args;
                if (!check(TokenKind::RParen))
                {
                    while (true)
                    {
                        auto arg_res = parse_pred();
                        if (std::holds_alternative<curlee::diag::Diagnostic>(arg_res))
                        {
                            return std::get<curlee::diag::Diagnostic>(std::move(arg_res));
                        }
                        args.push_back(std::get<Pred>(std::move(arg_res)));

                        if (match(TokenKind::Comma))
                        {
                            continue;
                        }
                        break;
                    }
                }
                if (auto err = consume(TokenKind::RParen, "expected ')' after predicate call");
                    err.has_value())
                {
                    return *err;
                }
                const Token rparen = previous();

                Pred pred;
                pred.span = span_cover(name.span, rparen.span);
                pred.node = PredCall{.callee = name.lexeme, .args = std::move(args)};
                return pred;
            }

            Pred pred;
            pred.span = name.span;
            pred.node = PredName{.name = name.lexeme};
            return pred;
        }

        if (match(TokenKind::LParen))
        {
            const Token l = previous();
            auto inner_res = parse_pred();
            if (std::holds_alternative<curlee::diag::Diagnostic>(inner_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(inner_res));
            }
            Pred inner = std::get<Pred>(std::move(inner_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after predicate");
                err.has_value())
            {
                return *err;
            }
            const Token r = previous();

            Pred pred;
            pred.span = span_cover(l.span, r.span);
            pred.node = PredGroup{.inner = std::make_unique<Pred>(std::move(inner))};
            return pred;
        }

        return error_at(peek(), "expected predicate");
    }

    [[nodiscard]] std::variant<Function, curlee::diag::Diagnostic> parse_function(bool is_extern =
                                                                                      false,
                                                                                  bool is_ghost =
                                                                                      false)
    {
        if (is_extern)
        {
            if (auto err = consume(TokenKind::KwExtern, "expected 'extern'"); err.has_value())
            {
                return *err;
            }
        }
        if (is_ghost)
        {
            if (auto err = consume(TokenKind::KwGhost, "expected 'ghost'"); err.has_value())
            {
                return *err;
            }
        }
        if (auto err = consume(TokenKind::KwFn, "expected 'fn'"); err.has_value())
        {
            return *err;
        }

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected function name");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::LParen, "expected '(' after function name");
            err.has_value())
        {
            return *err;
        }

        std::vector<Function::Param> params;
        if (!check(TokenKind::RParen))
        {
            while (true)
            {
                auto param_res = parse_param();
                if (std::holds_alternative<curlee::diag::Diagnostic>(param_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(param_res));
                }
                params.push_back(std::get<Function::Param>(std::move(param_res)));

                if (match(TokenKind::Comma))
                {
                    continue;
                }
                break;
            }
        }

        if (auto err = consume(TokenKind::RParen, "expected ')' after parameter list");
            err.has_value())
        {
            return *err;
        }

        std::optional<TypeName> return_type;
        if (match(TokenKind::Arrow))
        {
            auto type_res = parse_type();
            if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(type_res));
            }
            return_type = std::get<TypeName>(std::move(type_res));
        }

        std::vector<Pred> requires_clauses;
        std::vector<Pred> ensures;
        std::vector<GhostLetStmt> ghost_lets;
        if (match(TokenKind::LBracket))
        {
            while (!check(TokenKind::RBracket) && !is_at_end())
            {
                if (check(TokenKind::KwGhost))
                {
                    // `ghost let name[: Type] = expr;` — pre-state snapshot
                    // clause. The snapshot name is visible to this function's
                    // `requires`/`ensures` and (via call-site post-state
                    // inheritance) to callers.
                    auto ghost_res = parse_ghost_let_clause();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(ghost_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(std::move(ghost_res));
                    }
                    ghost_lets.push_back(std::get<GhostLetStmt>(std::move(ghost_res)));
                    continue;
                }

                if (match(TokenKind::KwRequires))
                {
                    auto pred_res = parse_pred();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                    }
                    requires_clauses.push_back(std::get<Pred>(std::move(pred_res)));
                }
                else if (match(TokenKind::KwEnsures))
                {
                    auto pred_res = parse_pred();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                    }
                    ensures.push_back(std::get<Pred>(std::move(pred_res)));
                }
                else
                {
                    return error_at(peek(),
                                    "expected 'requires', 'ensures', or 'ghost let' in contract block");
                }

                if (auto err = consume(TokenKind::Semicolon, "expected ';' after contract clause");
                    err.has_value())
                {
                    return *err;
                }
            }

            if (auto err = consume(TokenKind::RBracket, "expected ']' to end contract block");
                err.has_value())
            {
                return *err;
            }
        }

        // Extern functions are body-less declarations: `extern fn name(...) [contracts];`.
        // The implementation is resolved at link time by the host stub/runtime.
        if (is_extern)
        {
            // The contracts block above (if present) was consumed before this point.
            // Require a terminating semicolon; a body is an error.
            if (auto err = consume(TokenKind::Semicolon, "expected ';' after extern declaration");
                err.has_value())
            {
                return *err;
            }

            // An extern declaration has no body: synthesize an empty block so all
            // downstream consumers (resolver/type-checker/verifier/codegen) treat it
            // uniformly as a function whose body is empty. The is_extern flag is what
            // distinguishes a declaration from a definition.
            Function fn{
                .span = span_cover(name.span, previous().span),
                .name = name.lexeme,
                .body = Block{.span = name.span, .stmts = {}},
                .is_extern = true,
                .params = std::move(params),
                .requires_clauses = std::move(requires_clauses),
                .ensures = std::move(ensures),
                .ghost_lets = std::move(ghost_lets),
                .return_type = return_type,
            };
            return fn;
        }

        auto body_res = parse_block();
        if (std::holds_alternative<curlee::diag::Diagnostic>(body_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(body_res));
        }

        Block body = std::get<Block>(std::move(body_res));
        Function fn{
            .span = span_cover(name.span, body.span),
            .name = name.lexeme,
            .body = std::move(body),
            .is_extern = false,
            .is_ghost = is_ghost,
            .params = std::move(params),
            .requires_clauses = std::move(requires_clauses),
            .ensures = std::move(ensures),
            .ghost_lets = std::move(ghost_lets),
            .return_type = return_type,
        };
        return fn;
    }

    [[nodiscard]] std::variant<Block, curlee::diag::Diagnostic> parse_block()
    {
        if (auto err = consume(TokenKind::LBrace, "expected '{' to start block"); err.has_value())
        {
            return *err;
        }
        const Token lbrace = previous();

        std::vector<Stmt> stmts;
        while (!check(TokenKind::RBrace) && !is_at_end())
        {
            auto stmt_res = parse_stmt();
            if (std::holds_alternative<curlee::diag::Diagnostic>(stmt_res))
            {
                diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(stmt_res)));
                synchronize_stmt();
                continue;
            }
            stmts.push_back(std::get<Stmt>(std::move(stmt_res)));
        }

        if (auto err = consume(TokenKind::RBrace, "expected '}' to end block"); err.has_value())
        {
            return *err;
        }
        const Token rbrace = previous();

        return Block{.span = span_cover(lbrace.span, rbrace.span), .stmts = std::move(stmts)};
    }

    // Parse `ghost let name[: Type] = expr;` — a verification-only snapshot
    // binding used either as a body statement or as a contract-block clause
    // (where it is stored in Function::ghost_lets). The type annotation is
    // optional (inferred from the initializer).
    [[nodiscard]] std::variant<GhostLetStmt, curlee::diag::Diagnostic> parse_ghost_let_clause()
    {
        if (auto err = consume(TokenKind::KwGhost, "expected 'ghost'"); err.has_value())
        {
            return *err;
        }
        if (auto err = consume(TokenKind::KwLet, "expected 'let' after 'ghost'"); err.has_value())
        {
            return *err;
        }
        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected identifier after 'ghost let'");
        }
        const Token name = advance();

        std::optional<TypeName> type;
        if (match(TokenKind::Colon))
        {
            auto type_res = parse_type();
            if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(type_res));
            }
            type = std::get<TypeName>(std::move(type_res));
        }

        if (auto err = consume(TokenKind::Equal, "expected '=' in ghost let statement");
            err.has_value())
        {
            return *err;
        }

        auto expr_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
        }
        Expr value = std::get<Expr>(std::move(expr_res));

        if (auto err = consume(TokenKind::Semicolon, "expected ';' after ghost let statement");
            err.has_value())
        {
            return *err;
        }
        const Token semi = previous();

        GhostLetStmt ghost;
        ghost.name = name.lexeme;
        ghost.type = std::move(type);
        ghost.value = std::move(value);
        ghost.value.span = span_cover(name.span, semi.span);
        return ghost;
    }

    [[nodiscard]] std::variant<Stmt, curlee::diag::Diagnostic> parse_stmt()
    {
        const std::size_t start_pos = pos_;

        if (check(TokenKind::LBrace))
        {
            auto block_res = parse_block();
            if (std::holds_alternative<curlee::diag::Diagnostic>(block_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(block_res));
            }
            auto block = std::get<Block>(std::move(block_res));
            Stmt stmt;
            stmt.span = block.span;
            stmt.node = BlockStmt{.block = std::make_unique<Block>(std::move(block))};
            return stmt;
        }

        if (match(TokenKind::KwUnsafe))
        {
            const Token kw = previous();
            if (!check(TokenKind::LBrace))
            {
                return error_at(peek(), "expected '{' after 'unsafe'");
            }

            auto block_res = parse_block();
            if (std::holds_alternative<curlee::diag::Diagnostic>(block_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(block_res));
            }
            auto block = std::get<Block>(std::move(block_res));

            Stmt stmt;
            stmt.span = span_cover(kw.span, block.span);
            stmt.node = UnsafeStmt{.body = std::make_unique<Block>(std::move(block))};
            return stmt;
        }

        if (check(TokenKind::KwGhost))
        {
            // `ghost let name[: Type] = expr;` — verification-only snapshot
            // binding. The type annotation is optional (inferred from expr).
            auto ghost_res = parse_ghost_let_clause();
            if (std::holds_alternative<curlee::diag::Diagnostic>(ghost_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(ghost_res));
            }
            GhostLetStmt ghost = std::get<GhostLetStmt>(std::move(ghost_res));

            Stmt stmt{
                .span = ghost.value.span,
                .node = std::move(ghost),
            };
            return stmt;
        }

        if (match(TokenKind::KwLet))
        {
            const Token kw = previous();
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected identifier after 'let'");
            }
            const Token name = advance();

            if (auto err = consume(TokenKind::Colon, "expected ':' after let name");
                err.has_value())
            {
                return *err;
            }

            auto type_res = parse_type();
            if (std::holds_alternative<curlee::diag::Diagnostic>(type_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(type_res));
            }
            TypeName type = std::get<TypeName>(std::move(type_res));

            std::optional<Pred> refinement;
            if (match(TokenKind::KwWhere))
            {
                auto pred_res = parse_pred();
                if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                }
                refinement = std::get<Pred>(std::move(pred_res));
            }

            if (auto err = consume(TokenKind::Equal, "expected '=' in let statement");
                err.has_value())
            {
                return *err;
            }

            auto expr_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
            }
            Expr value = std::get<Expr>(std::move(expr_res));

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after let statement");
                err.has_value())
            {
                return *err;
            }
            const Token semi = previous();

            Stmt stmt{
                .span = span_cover(kw.span, semi.span),
                .node = LetStmt{.name = name.lexeme,
                                .type = std::move(type),
                                .refinement = std::move(refinement),
                                .value = std::move(value)},
            };
            return stmt;
        }

        if (match(TokenKind::KwIf))
        {
            const Token kw = previous();

            if (auto err = consume(TokenKind::LParen, "expected '(' after 'if'"); err.has_value())
            {
                return *err;
            }

            auto cond_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(cond_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(cond_res));
            }
            Expr cond = std::get<Expr>(std::move(cond_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after if condition");
                err.has_value())
            {
                return *err;
            }

            auto then_res = parse_block();
            if (std::holds_alternative<curlee::diag::Diagnostic>(then_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(then_res));
            }
            auto then_block = std::make_unique<Block>(std::get<Block>(std::move(then_res)));

            std::unique_ptr<Block> else_block;
            if (match(TokenKind::KwElse))
            {
                auto else_res = parse_block();
                if (std::holds_alternative<curlee::diag::Diagnostic>(else_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(else_res));
                }
                else_block = std::make_unique<Block>(std::get<Block>(std::move(else_res)));
            }

            const curlee::source::Span span = else_block != nullptr
                                                  ? span_cover(kw.span, else_block->span)
                                                  : span_cover(kw.span, then_block->span);
            Stmt stmt{
                .span = span,
                .node = IfStmt{.cond = std::move(cond),
                               .then_block = std::move(then_block),
                               .else_block = std::move(else_block)},
            };
            return stmt;
        }

        if (match(TokenKind::KwWhile))
        {
            const Token kw = previous();

            if (auto err = consume(TokenKind::LParen, "expected '(' after 'while'");
                err.has_value())
            {
                return *err;
            }

            auto cond_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(cond_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(cond_res));
            }
            Expr cond = std::get<Expr>(std::move(cond_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after while condition");
                err.has_value())
            {
                return *err;
            }

            auto body_res = parse_block();
            if (std::holds_alternative<curlee::diag::Diagnostic>(body_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(body_res));
            }
            auto body = std::make_unique<Block>(std::get<Block>(std::move(body_res)));

            Stmt stmt{
                .span = span_cover(kw.span, body->span),
                .node = WhileStmt{.cond = std::move(cond), .body = std::move(body)},
            };
            return stmt;
        }

        if (match(TokenKind::KwMatch))
        {
            const Token kw = previous();

            if (auto err = consume(TokenKind::LParen, "expected '(' after 'match'");
                err.has_value())
            {
                return *err;
            }

            auto value_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(value_res));
            }
            Expr value = std::get<Expr>(std::move(value_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after match value");
                err.has_value())
            {
                return *err;
            }

            if (auto err = consume(TokenKind::LBrace, "expected '{' after match value");
                err.has_value())
            {
                return *err;
            }

            std::vector<MatchArm> arms;
            while (!check(TokenKind::RBrace) && !is_at_end())
            {
                if (!check(TokenKind::Identifier))
                {
                    return error_at(peek(), "expected match arm pattern");
                }

                const Token enum_name = advance();
                if (auto err = consume(TokenKind::ColonColon, "expected '::' in match arm pattern");
                    err.has_value())
                {
                    return *err;
                }

                if (!check(TokenKind::Identifier))
                {
                    return error_at(peek(), "expected variant name in match arm pattern");
                }
                const Token variant_name = advance();

                std::optional<std::string_view> payload_name;
                curlee::source::Span pattern_span = span_cover(enum_name.span, variant_name.span);

                if (match(TokenKind::LParen))
                {
                    if (!check(TokenKind::Identifier))
                    {
                        return error_at(peek(), "expected payload binding name in match arm");
                    }
                    const Token payload = advance();
                    payload_name = payload.lexeme;
                    if (auto err = consume(TokenKind::RParen,
                                           "expected ')' after match arm payload binding");
                        err.has_value())
                    {
                        return *err;
                    }
                    pattern_span = span_cover(enum_name.span, previous().span);
                }

                if (auto err = consume(TokenKind::Equal, "expected '=>' after match arm pattern");
                    err.has_value())
                {
                    return *err;
                }
                if (auto err = consume(TokenKind::Greater, "expected '=>' after match arm pattern");
                    err.has_value())
                {
                    return *err;
                }

                auto body_res = parse_block();
                if (std::holds_alternative<curlee::diag::Diagnostic>(body_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(body_res));
                }
                auto body = std::make_unique<Block>(std::get<Block>(std::move(body_res)));

                arms.push_back(
                    MatchArm{.span = span_cover(pattern_span, body->span),
                             .pattern = MatchArmPattern{.span = pattern_span,
                                                        .enum_name = enum_name.lexeme,
                                                        .variant_name = variant_name.lexeme,
                                                        .payload_name = payload_name},
                             .body = std::move(body)});
            }

            if (auto err = consume(TokenKind::RBrace, "expected '}' after match arms");
                err.has_value())
            {
                return *err;
            }

            Stmt stmt;
            stmt.span = span_cover(kw.span, previous().span);
            stmt.node = MatchStmt{.value = std::move(value), .arms = std::move(arms)};
            return stmt;
        }

        if (match(TokenKind::KwReturn))
        {
            const Token kw = previous();
            std::optional<Expr> value;
            if (!check(TokenKind::Semicolon))
            {
                auto expr_res = parse_expr();
                if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
                }
                value = std::get<Expr>(std::move(expr_res));
            }

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after return statement");
                err.has_value())
            {
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
        if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
        }
        Expr expr = std::get<Expr>(std::move(expr_res));

        if (auto err = consume(TokenKind::Semicolon, "expected ';' after expression");
            err.has_value())
        {
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
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::OrOr))
        {
            const Token op = previous();
            auto rhs_res = parse_and();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_and()
    {
        auto lhs_res = parse_equality();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::AndAnd))
        {
            const Token op = previous();
            auto rhs_res = parse_equality();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_equality()
    {
        auto lhs_res = parse_comparison();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_comparison();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_comparison()
    {
        auto lhs_res = parse_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
               match(TokenKind::GreaterEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_term();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_term()
    {
        auto lhs_res = parse_factor();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Plus) || match(TokenKind::Minus))
        {
            const Token op = previous();
            auto rhs_res = parse_factor();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_factor()
    {
        auto lhs_res = parse_unary();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Star) || match(TokenKind::Slash))
        {
            const Token op = previous();
            auto rhs_res = parse_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr rhs = std::get<Expr>(std::move(rhs_res));

            const curlee::source::Span combined_span = span_cover(expr.span, rhs.span);
            auto lhs_ptr = std::make_unique<Expr>(std::move(expr));
            auto rhs_ptr = std::make_unique<Expr>(std::move(rhs));
            expr = Expr{.span = combined_span,
                        .node = BinaryExpr{
                            .op = op.kind,
                            .lhs = std::move(lhs_ptr),
                            .rhs = std::move(rhs_ptr),
                        }};
        }

        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_unary()
    {
        if (match(TokenKind::Bang) || match(TokenKind::Minus))
        {
            const Token op = previous();
            auto rhs_res = parse_unary();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
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
        if (std::holds_alternative<curlee::diag::Diagnostic>(callee_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(callee_res));
        }

        Expr expr = std::get<Expr>(std::move(callee_res));

        while (true)
        {
            if (match(TokenKind::Dot))
            {
                const Token dot = previous();
                if (!check(TokenKind::Identifier))
                {
                    return error_at(peek(), "expected identifier after '.'");
                }
                const Token member = advance();

                // Physical memory read: `base.read()`.
                if (member.lexeme == "read" && check(TokenKind::LParen))
                {
                    advance(); // consume '('
                    if (!check(TokenKind::RParen))
                    {
                        return error_at(peek(), "read() expects no arguments");
                    }
                    const Token rparen = advance();
                    Expr access;
                    access.span = span_cover(expr.span, rparen.span);
                    access.node = curlee::parser::PhysReadExpr{
                        .base = std::make_unique<Expr>(std::move(expr))};
                    (void)dot;
                    expr = std::move(access);
                    continue;
                }

                // Physical memory write: `base.write(v)`.
                if (member.lexeme == "write" && check(TokenKind::LParen))
                {
                    advance(); // consume '('
                    Expr value;
                    if (check(TokenKind::PhysAddrLiteral))
                    {
                        // Hex/underscore constant: only meaningful as a phys write value,
                        // so it is consumed here and wrapped as a constant Int expression.
                        const Token lit = advance();
                        value.span = lit.span;
                        value.node = curlee::parser::IntExpr{.lexeme = lit.lexeme};
                    }
                    else
                    {
                        auto value_res = parse_expr();
                        if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
                        {
                            return std::get<curlee::diag::Diagnostic>(std::move(value_res));
                        }
                        value = std::get<Expr>(std::move(value_res));
                    }
                    if (auto err = consume(TokenKind::RParen, "expected ')' after write(...)");
                        err.has_value())
                    {
                        return *err;
                    }
                    const Token rparen = previous();
                    Expr access;
                    access.span = span_cover(expr.span, rparen.span);
                    access.node = curlee::parser::PhysWriteExpr{
                        .base = std::make_unique<Expr>(std::move(expr)),
                        .value = std::make_unique<Expr>(std::move(value))};
                    (void)dot;
                    expr = std::move(access);
                    continue;
                }

                Expr access;
                access.span = span_cover(expr.span, member.span);
                access.node = MemberExpr{.base = std::make_unique<Expr>(std::move(expr)),
                                         .member = member.lexeme};
                (void)dot;
                expr = std::move(access);
                continue;
            }

            if (!match(TokenKind::LParen))
            {
                break;
            }

            const Token lparen = previous();

            std::vector<Expr> args;
            if (!check(TokenKind::RParen))
            {
                while (true)
                {
                    auto arg_res = parse_expr();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(arg_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(std::move(arg_res));
                    }
                    args.push_back(std::get<Expr>(std::move(arg_res)));

                    if (match(TokenKind::Comma))
                    {
                        continue;
                    }
                    break;
                }
            }

            if (auto err = consume(TokenKind::RParen, "expected ')' after arguments");
                err.has_value())
            {
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

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic>
    parse_struct_literal_after_name(const Token& type_name)
    {
        if (auto err = consume(TokenKind::LBrace, "expected '{' to start struct literal");
            err.has_value())
        {
            return *err;
        }

        std::vector<StructLiteralExprField> fields;
        std::unordered_map<std::string_view, curlee::source::Span> seen;

        while (!check(TokenKind::RBrace) && !is_at_end())
        {
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected field name in struct literal");
            }
            const Token field_name = advance();

            if (auto it = seen.find(field_name.lexeme); it != seen.end())
            {
                auto d = error_at(field_name, "duplicate field in struct literal");
                set_notes1(d, "previous field initializer is here", it->second);
                return d;
            }
            seen.emplace(field_name.lexeme, field_name.span);

            if (auto err = consume(TokenKind::Colon, "expected ':' after field name");
                err.has_value())
            {
                return *err;
            }

            auto value_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(value_res));
            }
            Expr value = std::get<Expr>(std::move(value_res));

            const curlee::source::Span field_span = span_cover(field_name.span, value.span);
            fields.push_back(StructLiteralExprField{
                .span = field_span,
                .name = field_name.lexeme,
                .value = std::make_unique<Expr>(std::move(value)),
            });

            if (match(TokenKind::Comma))
            {
                // Allow trailing comma.
                if (check(TokenKind::RBrace))
                {
                    break;
                }
                continue;
            }

            if (check(TokenKind::RBrace))
            {
                break;
            }

            return error_at(peek(), "expected ',' or '}' after field initializer");
        }

        if (auto err = consume(TokenKind::RBrace, "expected '}' after struct literal");
            err.has_value())
        {
            return *err;
        }
        const Token rbrace = previous();

        Expr expr;
        expr.span = span_cover(type_name.span, rbrace.span);
        expr.node = StructLiteralExpr{.type_name = type_name.lexeme, .fields = std::move(fields)};
        return expr;
    }

    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_primary()
    {
        if (match(TokenKind::IntLiteral))
        {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = IntExpr{.lexeme = lit.lexeme};
            return expr;
        }

        if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse))
        {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = BoolExpr{.value = (lit.kind == TokenKind::KwTrue)};
            return expr;
        }

        if (match(TokenKind::StringLiteral))
        {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = StringExpr{.lexeme = lit.lexeme};
            return expr;
        }

        if (match(TokenKind::Identifier))
        {
            const Token name = previous();

            if (match(TokenKind::ColonColon))
            {
                if (!check(TokenKind::Identifier))
                {
                    return error_at(peek(), "expected identifier after '::'");
                }
                const Token rhs = advance();
                Expr expr;
                expr.span = span_cover(name.span, rhs.span);
                expr.node = ScopedNameExpr{.lhs = name.lexeme, .rhs = rhs.lexeme};
                return expr;
            }

            if (check(TokenKind::LBrace))
            {
                return parse_struct_literal_after_name(name);
            }

            Expr expr;
            expr.span = name.span;
            expr.node = NameExpr{.name = name.lexeme};
            return expr;
        }

        // Physical memory literal: `phys<U>(literal)`.
        if (match(TokenKind::KwPhys))
        {
            const Token kw = previous();
            if (auto err = consume(TokenKind::Less, "expected '<' after 'phys'");
                err.has_value())
            {
                return *err;
            }
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected element type after 'phys<'");
            }
            const Token elem = advance();
            if (auto err = consume(TokenKind::Greater, "expected '>' after element type");
                err.has_value())
            {
                return *err;
            }
            if (auto err = consume(TokenKind::LParen, "expected '(' after 'phys<U>'");
                err.has_value())
            {
                return *err;
            }
            // The address must be a compile-time literal: decimal (IntLiteral) or
            // hex/underscore form (PhysAddrLiteral). Both are constant-only.
            if (!check(TokenKind::IntLiteral) && !check(TokenKind::PhysAddrLiteral))
            {
                return error_at(peek(), "phys() expects an integer literal address");
            }
            const Token addr = advance();
            if (auto err = consume(TokenKind::RParen, "expected ')' after phys address");
                err.has_value())
            {
                return *err;
            }
            const Token rparen = previous();

            Expr expr;
            expr.span = span_cover(kw.span, rparen.span);
            expr.node = curlee::parser::PhysExpr{.element_kind = elem.lexeme,
                                                 .lexeme = addr.lexeme};
            return expr;
        }

        if (match(TokenKind::LParen))
        {
            const Token l = previous();
            auto inner_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(inner_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(inner_res));
            }
            Expr inner = std::get<Expr>(std::move(inner_res));

            if (auto err = consume(TokenKind::RParen, "expected ')' after expression");
                err.has_value())
            {
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

class Dumper
{
  public:
    explicit Dumper(std::ostringstream& out) : out_(out) {}

    void dump_program(const Program& p)
    {
        for (const auto& imp : p.imports)
        {
            out_ << "import ";
            for (std::size_t i = 0; i < imp.path.size(); ++i)
            {
                out_ << imp.path[i];
                if (i + 1 < imp.path.size())
                {
                    out_ << ".";
                }
            }
            if (imp.alias.has_value())
            {
                out_ << " as " << *imp.alias;
            }
            out_ << ";\n";
        }

        const bool has_types = !p.structs.empty() || !p.enums.empty();
        if (!p.imports.empty() && (has_types || !p.functions.empty()))
        {
            out_ << "\n";
        }

        for (std::size_t i = 0; i < p.structs.size(); ++i)
        {
            dump_struct_decl(p.structs[i]);
            if (i + 1 < p.structs.size())
            {
                out_ << "\n";
            }
        }

        if (!p.structs.empty() && !p.enums.empty())
        {
            out_ << "\n";
        }

        for (std::size_t i = 0; i < p.enums.size(); ++i)
        {
            dump_enum_decl(p.enums[i]);
            if (i + 1 < p.enums.size())
            {
                out_ << "\n";
            }
        }

        if (has_types && !p.functions.empty())
        {
            out_ << "\n";
        }

        for (std::size_t i = 0; i < p.functions.size(); ++i)
        {
            dump_function(p.functions[i]);
            if (i + 1 < p.functions.size())
            {
                out_ << "\n";
            }
        }
    }

  private:
    std::ostringstream& out_;

    void dump_type(const TypeName& t)
    {
        if (t.is_capability)
        {
            out_ << "cap " << t.name;
            return;
        }
        out_ << t.name;
        if (t.type_arg.has_value())
        {
            out_ << "<" << *t.type_arg << ">";
        }
    }

    void dump_struct_decl(const StructDecl& s)
    {
        out_ << "struct " << s.name << " {";
        for (const auto& f : s.fields)
        {
            out_ << " " << f.name << ": ";
            dump_type(f.type);
            out_ << ";";
        }
        out_ << " }\n";
    }

    void dump_enum_decl(const EnumDecl& e)
    {
        out_ << "enum " << e.name << " {";
        for (const auto& v : e.variants)
        {
            out_ << " " << v.name;
            if (v.payload.has_value())
            {
                out_ << "(";
                dump_type(*v.payload);
                out_ << ")";
            }
            out_ << ";";
        }
        out_ << " }\n";
    }

    void dump_function(const Function& f)
    {
        if (f.is_extern)
        {
            out_ << "extern fn " << f.name << "(";
        }
        else if (f.is_ghost)
        {
            out_ << "ghost fn " << f.name << "(";
        }
        else
        {
            out_ << "fn " << f.name << "(";
        }
        for (std::size_t i = 0; i < f.params.size(); ++i)
        {
            const auto& p = f.params[i];
            out_ << p.name << ": ";
            dump_type(p.type);
            if (p.refinement.has_value())
            {
                out_ << " where ";
                dump_pred(*p.refinement);
            }
            if (i + 1 < f.params.size())
            {
                out_ << ", ";
            }
        }
        out_ << ")";
        if (f.return_type.has_value())
        {
            out_ << " -> ";
            dump_type(*f.return_type);
        }

        if (!f.requires_clauses.empty() || !f.ensures.empty() || !f.ghost_lets.empty())
        {
            out_ << " [";
            for (const auto& g : f.ghost_lets)
            {
                out_ << " ghost let " << g.name;
                if (g.type.has_value())
                {
                    out_ << ": ";
                    dump_type(*g.type);
                }
                out_ << " = ";
                dump_expr(g.value);
                out_ << ";";
            }
            for (const auto& r : f.requires_clauses)
            {
                out_ << " requires ";
                dump_pred(r);
                out_ << ";";
            }
            for (const auto& e : f.ensures)
            {
                out_ << " ensures ";
                dump_pred(e);
                out_ << ";";
            }
            out_ << " ]";
        }
        if (f.is_extern)
        {
            // Body-less declaration: dump as `extern fn ... ;` rather than a
            // synthesized empty block.
            out_ << " ;\n";
            return;
        }
        out_ << " ";
        dump_block(f.body);
    }

    void dump_block(const Block& b)
    {
        out_ << "{";
        for (const auto& s : b.stmts)
        {
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
        out_ << "let " << s.name << ": ";
        dump_type(s.type);
        if (s.refinement.has_value())
        {
            out_ << " where ";
            dump_pred(*s.refinement);
        }
        out_ << " = ";
        dump_expr(s.value);
        out_ << ";";
    }

    void dump_stmt_node(const GhostLetStmt& s)
    {
        out_ << "ghost let " << s.name;
        if (s.type.has_value())
        {
            out_ << ": ";
            dump_type(*s.type);
        }
        out_ << " = ";
        dump_expr(s.value);
        out_ << ";";
    }

    void dump_stmt_node(const ReturnStmt& s)
    {
        if (!s.value.has_value())
        {
            out_ << "return;";
            return;
        }

        out_ << "return ";
        dump_expr(*s.value);
        out_ << ";";
    }

    void dump_stmt_node(const ExprStmt& s)
    {
        dump_expr(s.expr);
        out_ << ";";
    }

    void dump_stmt_node(const BlockStmt& s) { dump_block(*s.block); }

    void dump_stmt_node(const UnsafeStmt& s)
    {
        out_ << "unsafe ";
        dump_block(*s.body);
    }

    void dump_stmt_node(const IfStmt& s)
    {
        out_ << "if (";
        dump_expr(s.cond);
        out_ << ") ";
        dump_block(*s.then_block);
        if (s.else_block != nullptr)
        {
            out_ << " else ";
            dump_block(*s.else_block);
        }
    }

    void dump_stmt_node(const WhileStmt& s)
    {
        out_ << "while (";
        dump_expr(s.cond);
        out_ << ") ";
        dump_block(*s.body);
    }

    void dump_stmt_node(const MatchStmt& s)
    {
        out_ << "match (";
        dump_expr(s.value);
        out_ << ") {";
        for (const auto& arm : s.arms)
        {
            out_ << " " << arm.pattern.enum_name << "::" << arm.pattern.variant_name;
            if (arm.pattern.payload_name.has_value())
            {
                out_ << "(" << *arm.pattern.payload_name << ")";
            }
            out_ << " => ";
            dump_block(*arm.body);
        }
        out_ << " }";
    }

    void dump_expr(const Expr& e)
    {
        std::visit([&](const auto& node) { dump_expr_node(node); }, e.node);
    }

    void dump_expr_node(const IntExpr& e) { out_ << e.lexeme; }
    void dump_expr_node(const BoolExpr& e) { out_ << (e.value ? "true" : "false"); }
    void dump_expr_node(const StringExpr& e) { out_ << e.lexeme; }
    void dump_expr_node(const NameExpr& e) { out_ << e.name; }

    void dump_expr_node(const ScopedNameExpr& e) { out_ << e.lhs << "::" << e.rhs; }

    void dump_expr_node(const MemberExpr& e)
    {
        dump_expr(*e.base);
        out_ << "." << e.member;
    }

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
        for (std::size_t i = 0; i < e.args.size(); ++i)
        {
            dump_expr(e.args[i]);
            if (i + 1 < e.args.size())
            {
                out_ << ", ";
            }
        }
        out_ << ")";
    }

    void dump_expr_node(const StructLiteralExpr& e)
    {
        out_ << e.type_name << "{";
        for (std::size_t i = 0; i < e.fields.size(); ++i)
        {
            const auto& f = e.fields[i];
            out_ << " " << f.name << ": ";
            dump_expr(*f.value);
            if (i + 1 < e.fields.size())
            {
                out_ << ",";
            }
        }
        if (!e.fields.empty())
        {
            out_ << " ";
        }
        out_ << "}";
    }

    void dump_expr_node(const PhysExpr& e)
    {
        out_ << "phys<" << e.element_kind << ">(" << e.lexeme << ")";
    }

    void dump_expr_node(const PhysReadExpr& e)
    {
        dump_expr(*e.base);
        out_ << ".read()";
    }

    void dump_expr_node(const PhysWriteExpr& e)
    {
        dump_expr(*e.base);
        out_ << ".write(";
        dump_expr(*e.value);
        out_ << ")";
    }

    void dump_pred(const Pred& p)
    {
        std::visit([&](const auto& node) { dump_pred_node(node); }, p.node);
    }

    void dump_pred_node(const PredInt& p) { out_ << p.lexeme; }
    void dump_pred_node(const PredBool& p) { out_ << (p.value ? "true" : "false"); }
    void dump_pred_node(const PredName& p) { out_ << p.name; }

    void dump_pred_node(const PredCall& p)
    {
        out_ << p.callee << "(";
        for (std::size_t i = 0; i < p.args.size(); ++i)
        {
            if (i > 0)
            {
                out_ << ", ";
            }
            dump_pred(p.args[i]);
        }
        out_ << ")";
    }

    void dump_pred_node(const PredGroup& p)
    {
        out_ << "(";
        dump_pred(*p.inner);
        out_ << ")";
    }

    void dump_pred_node(const PredUnary& p)
    {
        out_ << curlee::lexer::to_string(p.op) << " ";
        dump_pred(*p.rhs);
    }

    void dump_pred_node(const PredBinary& p)
    {
        out_ << "(";
        dump_pred(*p.lhs);
        out_ << " " << curlee::lexer::to_string(p.op) << " ";
        dump_pred(*p.rhs);
        out_ << ")";
    }
};

} // namespace

ParseResult parse(std::span<const curlee::lexer::Token> tokens)
{
    auto result = Parser(tokens).parse_program();
    if (auto* program = std::get_if<Program>(&result))
    {
        assign_expr_ids_program(*program);
    }
    return result;
} // GCOVR_EXCL_LINE

void reassign_expr_ids(Program& program)
{
    assign_expr_ids_program(program);
}

std::string dump(const Program& program)
{
    std::ostringstream out;
    Dumper d(out);
    d.dump_program(program);
    return out.str();
}

} // namespace curlee::parser
