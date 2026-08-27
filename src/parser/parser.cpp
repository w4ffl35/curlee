// SPDX-License-Identifier: MIT
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <curlee/lexer/token.h>
#include <curlee/parser/parser.h>
#include <limits>
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
            else if constexpr (std::is_same_v<Node, curlee::parser::AssignStmt>)
            {
                assign_expr_ids(node.value, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::IndexAssignStmt>)
            {
                assign_expr_ids(node.index, next_id);
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
            else if constexpr (std::is_same_v<Node, curlee::parser::PortIOExpr>)
            {
                assign_expr_ids(*node.port, next_id);
                if (node.value != nullptr)
                {
                    assign_expr_ids(*node.value, next_id);
                }
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::RuntimePhysReadExpr>)
            {
                assign_expr_ids(*node.addr, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::RuntimePhysWriteExpr>)
            {
                assign_expr_ids(*node.addr, next_id);
                assign_expr_ids(*node.value, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::AddrOfExpr>)
            {
                if (node.target != nullptr)
                {
                    assign_expr_ids(*node.target, next_id);
                }
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
            else if constexpr (std::is_same_v<Node, curlee::parser::IndexExpr>)
            {
                assign_expr_ids(*node.base, next_id);
                assign_expr_ids(*node.index, next_id);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::ArrayLiteralExpr>)
            {
                assign_expr_ids(*node.value, next_id);
            }
        },
        expr.node);
}

void assign_expr_ids_program(curlee::parser::Program& program)
{
    std::size_t next_id = 1;
    // Module-level static initializers are ordinary expressions from the
    // front-end's perspective: give them ids so the type checker can record
    // their inferred types (issue #287).
    for (auto& stat : program.statics)
    {
        assign_expr_ids(stat.value, next_id);
    }
    for (auto& function : program.functions)
    {
        assign_expr_ids_block(function.body, next_id);
    }
}

class Parser
{
  public:
    explicit Parser(std::span<const Token> tokens, std::span<const BuildDefine> defines = {})
        : tokens_(tokens), defines_(defines.begin(), defines.end())
    {
    }

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

            // Module-level mutable state (issue #287): `static name: Type = expr;`.
            // A `static` at the top level declares a file-scope mutable binding
            // (scalar or [T; N] array) initialized once, persisting across calls.
            if (check(TokenKind::KwStatic))
            {
                if (!seen_non_import)
                {
                    first_non_import_span = peek().span;
                }
                seen_non_import = true;

                auto s = parse_static_decl(/*is_extern=*/false);
                if (std::holds_alternative<curlee::diag::Diagnostic>(s))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(s)));
                    synchronize_top_level();
                    continue;
                }
                program.statics.push_back(std::get<StaticVarDecl>(std::move(s)));
                continue;
            }

            // `extern static name: Type = expr;` (issue #297): the same
            // module-level mutable binding with EXTERNAL linkage. The codegen
            // emits a plain file-scope C global (no `static`, verbatim
            // identifier) so hand-written boot assembly or C linked into the
            // same final image can write it before `curlee_main` runs and
            // Curlee reads it back via a normal name reference.
            if (check(TokenKind::KwExtern) && check_next(TokenKind::KwStatic))
            {
                if (!seen_non_import)
                {
                    first_non_import_span = peek().span;
                }
                seen_non_import = true;

                auto s = parse_static_decl(/*is_extern=*/true);
                if (std::holds_alternative<curlee::diag::Diagnostic>(s))
                {
                    diagnostics_.push_back(std::get<curlee::diag::Diagnostic>(std::move(s)));
                    synchronize_top_level();
                    continue;
                }
                program.statics.push_back(std::get<StaticVarDecl>(std::move(s)));
                continue;
            }

            if (check(TokenKind::KwFn) || check(TokenKind::KwExtern) || check(TokenKind::KwGhost))
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

            diagnostics_.push_back(error_at(
                peek(), "expected 'import', 'struct', 'enum', 'static', 'fn', 'extern fn', or "
                        "'extern static'"));
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
    // Build-time constants injected via `--define NAME=VALUE` (issue #296),
    // copied so synthesized lexemes can reference their name strings.
    std::vector<BuildDefine> defines_;
    std::size_t pos_ = 0;
    std::vector<curlee::diag::Diagnostic> diagnostics_;

    [[nodiscard]] bool is_at_end() const { return peek().kind == TokenKind::Eof; }

    // Look up a `--define` build constant by name (issue #296). Returns
    // std::nullopt when the name is not a provided define (callers then treat
    // it as an ordinary identifier).
    [[nodiscard]] std::optional<std::uint64_t> lookup_define(std::string_view name) const
    {
        for (const auto& d : defines_)
        {
            if (d.name == name)
            {
                return d.value;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] const Token& peek() const
    {
        assert(pos_ < tokens_.size()); // GCOVR_EXCL_LINE
        return tokens_[pos_];
    }

    // True when the token AFTER the current one has `kind` (bounds-safe; used
    // to disambiguate `extern fn` from `extern static` at the top level).
    [[nodiscard]] bool check_next(TokenKind kind) const
    {
        if (pos_ + 1 >= tokens_.size())
        {
            return false;
        }
        return tokens_[pos_ + 1].kind == kind;
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

    // Parse and constant-fold an array length / repeat count: the MVP form is
    // a single positive integer literal (issue #278); with `--define` build
    // constants (issue #296) the length may also be a constant expression over
    // defines and literals — `[T; W * H]`, `[T; SLOTS * W * H]`,
    // `[T; PAGES * 4096]` — folded here to a plain decimal string. Grammar
    // (all operands non-negative constants; `*` binds tighter than `+`):
    //
    //   length := term ('+' term)*
    //   term   := factor ('*' factor)*
    //   factor := IntLiteral | '(' length ')' | define-name
    //
    // The folded value is a plain decimal string (hex/underscore lexemes are
    // not length forms — hex stays reserved for physical-address literals).
    // Returns the decimal string or a diagnostic. The positivity check lives
    // in the type checker (as it does for plain literal lengths), so a folded
    // 0 gets the same "positive integer literal" diagnostic as a source
    // `[T; 0]`.
    [[nodiscard]] std::variant<std::string, curlee::diag::Diagnostic>
    parse_array_length_constant()
    {
        using LengthValue = std::variant<std::uint64_t, curlee::diag::Diagnostic>;

        // One factor: a decimal IntLiteral, a `--define` name, or a
        // parenthesized sub-expression.
        auto parse_factor = [&]() -> LengthValue
        {
            if (match(TokenKind::IntLiteral))
            {
                const Token lit = previous();
                std::string cleaned;
                cleaned.reserve(lit.lexeme.size());
                for (const char c : lit.lexeme)
                {
                    if (c != '_')
                    {
                        cleaned.push_back(c);
                    }
                }
                std::uint64_t v = 0;
                const auto res = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), v);
                if (res.ec != std::errc{} || res.ptr != cleaned.data() + cleaned.size())
                {
                    return error_at(lit, "array length constant is too large");
                }
                return v;
            }
            if (check(TokenKind::Identifier))
            {
                const auto define = lookup_define(peek().lexeme);
                if (define.has_value())
                {
                    advance();
                    return *define;
                }
            }
            if (match(TokenKind::LParen))
            {
                auto inner = parse_array_length_constant();
                if (std::holds_alternative<curlee::diag::Diagnostic>(inner))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(inner));
                }
                // Re-fold the parenthesized decimal string (it cannot
                // overflow — it was already folded on the way out).
                std::uint64_t v = 0;
                for (const char c : std::get<std::string>(inner))
                {
                    v = v * 10 + static_cast<std::uint64_t>(c - '0');
                }
                if (auto err = consume(TokenKind::RParen, "expected ')' after array length");
                    err.has_value())
                {
                    return *err;
                }
                return v;
            }
            return error_at(peek(),
                            "array length must be an integer literal or a build-time constant");
        };

        // One `*`-separated product of factors.
        auto parse_term = [&]() -> LengthValue
        {
            auto first = parse_factor();
            if (std::holds_alternative<curlee::diag::Diagnostic>(first))
            {
                return first;
            }
            std::uint64_t product = std::get<std::uint64_t>(first);
            while (check(TokenKind::Star))
            {
                advance(); // consume '*'
                auto next = parse_factor();
                if (std::holds_alternative<curlee::diag::Diagnostic>(next))
                {
                    return next;
                }
                const std::uint64_t factor = std::get<std::uint64_t>(next);
                if (factor != 0 && product > (std::numeric_limits<std::uint64_t>::max)() / factor)
                {
                    return error_at(previous(), "array length constant overflow");
                }
                product *= factor;
            }
            return product;
        };

        // One `+`-separated sum of products.
        auto first_term = parse_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(first_term))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(first_term));
        }
        std::uint64_t total = std::get<std::uint64_t>(first_term);
        while (check(TokenKind::Plus))
        {
            advance(); // consume '+'
            auto next_term = parse_term();
            if (std::holds_alternative<curlee::diag::Diagnostic>(next_term))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(next_term));
            }
            const std::uint64_t term = std::get<std::uint64_t>(next_term);
            if (term > (std::numeric_limits<std::uint64_t>::max)() - total)
            {
                return error_at(previous(), "array length constant overflow");
            }
            total += term;
        }
        return std::to_string(total);
    }

    [[nodiscard]] std::variant<TypeName, curlee::diag::Diagnostic> parse_type()
    {
        // Fixed-size array type: `[T; N]` (issue #278). T is a storable core
        // element type (Int/U8/U16/U32/U64) and N is a compile-time constant
        // expression: a positive integer literal, or — with `--define` build
        // constants (issue #296) — a constant expression over build defines
        // and literals (e.g. `[U32; ASSET_W * ASSET_H]`), folded to its
        // decimal value here so every downstream consumer sees a plain length.
        if (match(TokenKind::LBracket))
        {
            const Token lbracket = previous();
            if (!check(TokenKind::Identifier))
            {
                return error_at(peek(), "expected element type name after '[' in array type");
            }
            const Token elem = advance();
            if (auto err = consume(TokenKind::Semicolon, "expected ';' after array element type");
                err.has_value())
            {
                return *err;
            }
            auto len_res = parse_array_length_constant();
            if (std::holds_alternative<curlee::diag::Diagnostic>(len_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(len_res));
            }
            if (auto err = consume(TokenKind::RBracket, "expected ']' after array length");
                err.has_value())
            {
                return *err;
            }
            const Token rbracket = previous();
            return TypeName{
                .span = curlee::source::Span{.start = lbracket.span.start, .end = rbracket.span.end},
                .is_capability = false,
                .name = elem.lexeme,
                .type_arg = std::nullopt,
                .array_len = std::get<std::string>(std::move(len_res))};
        }

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
                const bool seg_is_ident = check(TokenKind::Identifier) || check(TokenKind::KwPhys);
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
                            .type_arg = std::nullopt,
                            .array_len = std::nullopt};
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
                            .type_arg = arg.lexeme,
                            .array_len = std::nullopt};
        }
        return TypeName{.span = t.span,
                        .is_capability = false,
                        .name = t.lexeme,
                        .type_arg = std::nullopt,
                        .array_len = std::nullopt};
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

    // Module-level mutable state declaration (issue #287):
    //   static name: Type = expr;
    // and the external-linkage form (issue #297):
    //   extern static name: Type = expr;
    // Type is a storable scalar (Int/Bool/U8/U16/U32/U64) or a fixed-size
    // array `[T; N]`; expr must be a compile-time literal (Int/Bool literal for
    // scalars, `[v; N]` repeat literal for arrays). The literal restriction is
    // enforced by the type checker, which has the type information. `is_extern`
    // selects the `extern static` form: the codegen emits a plain file-scope C
    // global (no `static`, verbatim identifier) so external boot code can write
    // it before `curlee_main` runs.
    [[nodiscard]] std::variant<StaticVarDecl, curlee::diag::Diagnostic>
    parse_static_decl(bool is_extern)
    {
        if (is_extern)
        {
            if (auto err = consume(TokenKind::KwExtern, "expected 'extern'"); err.has_value())
            {
                return *err;
            }
        }
        if (auto err = consume(TokenKind::KwStatic, "expected 'static'"); err.has_value())
        {
            return *err;
        }
        const Token kw = previous();

        if (!check(TokenKind::Identifier))
        {
            return error_at(peek(), "expected variable name after 'static'");
        }
        const Token name = advance();

        if (auto err = consume(TokenKind::Colon, "expected ':' after static variable name");
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

        if (auto err = consume(TokenKind::Equal, "expected '=' in static declaration");
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

        if (auto err = consume(TokenKind::Semicolon, "expected ';' after static declaration");
            err.has_value())
        {
            return *err;
        }
        const Token semi = previous();

        return StaticVarDecl{.span = span_cover(kw.span, semi.span),
                             .name = name.lexeme,
                             .type = std::move(type),
                             .value = std::move(value),
                             .is_extern = is_extern};
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

    // Bitwise or: lowest-precedence bitwise operator (just above logical `&&`).
    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_bit_or()
    {
        auto lhs_res = parse_pred_bit_xor();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Pipe))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_bit_xor();
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

    // Bitwise xor: binds tighter than bitwise or, looser than bitwise and.
    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_bit_xor()
    {
        auto lhs_res = parse_pred_bit_and();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Caret))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_bit_and();
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

    // Bitwise and: binds tighter than xor, looser than equality (C precedence).
    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_bit_and()
    {
        auto lhs_res = parse_pred_equality();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Amp))
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

    // Logical `&&` binds looser than all bitwise operators (C precedence:
    // || < && < | < ^ < & < ==), so it parses bitwise or as its operand.
    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_and()
    {
        auto lhs_res = parse_pred_bit_or();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::AndAnd))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_bit_or();
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
        auto lhs_res = parse_pred_shift();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
               match(TokenKind::GreaterEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_pred_shift();
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

    // Shifts bind tighter than comparisons, looser than additive (C-like).
    [[nodiscard]] std::variant<Pred, curlee::diag::Diagnostic> parse_pred_shift()
    {
        auto lhs_res = parse_pred_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Pred pred = std::get<Pred>(std::move(lhs_res));

        while (match(TokenKind::ShiftLeft) || match(TokenKind::ShiftRight))
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

        while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent))
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
        if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Tilde))
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

    [[nodiscard]] std::variant<Function, curlee::diag::Diagnostic>
    parse_function(bool is_extern = false, bool is_ghost = false)
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
        std::optional<Pred> fuel_bound;
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
                else if (match(TokenKind::KwFuel))
                {
                    // Static WCET bound: `fuel <pred>;` — an Int-valued expression
                    // over the function's inputs bounding worst-case cost. A
                    // function may declare at most one fuel clause.
                    auto pred_res = parse_pred();
                    if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                    {
                        return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                    }
                    if (fuel_bound.has_value())
                    {
                        return error_at(peek(), "duplicate 'fuel' clause in contract block");
                    }
                    fuel_bound = std::get<Pred>(std::move(pred_res));
                }
                else
                {
                    return error_at(
                        peek(),
                        "expected 'requires', 'ensures', 'ghost let', or 'fuel' in contract block");
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
                .fuel_bound = std::move(fuel_bound),
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
            .fuel_bound = std::move(fuel_bound),
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

            // Optional loop contract block: `[ invariant ...; decreases ...; ]`.
            // A loop with clauses is verified with the loop-invariant proof
            // obligations; without them it keeps the legacy single-pass mode.
            std::vector<Pred> invariants;
            std::optional<Pred> decreases;
            if (match(TokenKind::LBracket))
            {
                while (!check(TokenKind::RBracket) && !is_at_end())
                {
                    if (match(TokenKind::KwInvariant))
                    {
                        auto pred_res = parse_pred();
                        if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                        {
                            return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                        }
                        invariants.push_back(std::get<Pred>(std::move(pred_res)));
                    }
                    else if (match(TokenKind::KwDecreases))
                    {
                        if (decreases.has_value())
                        {
                            return error_at(peek(),
                                            "duplicate 'decreases' clause in loop contract");
                        }
                        auto pred_res = parse_pred();
                        if (std::holds_alternative<curlee::diag::Diagnostic>(pred_res))
                        {
                            return std::get<curlee::diag::Diagnostic>(std::move(pred_res));
                        }
                        decreases = std::get<Pred>(std::move(pred_res));
                    }
                    else
                    {
                        return error_at(
                            peek(), "expected 'invariant' or 'decreases' in loop contract block");
                    }

                    if (auto err = consume(TokenKind::Semicolon,
                                           "expected ';' after loop contract clause");
                        err.has_value())
                    {
                        return *err;
                    }
                }

                if (auto err =
                        consume(TokenKind::RBracket, "expected ']' to end loop contract block");
                    err.has_value())
                {
                    return *err;
                }
            }

            auto body_res = parse_block();
            if (std::holds_alternative<curlee::diag::Diagnostic>(body_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(body_res));
            }
            auto body = std::make_unique<Block>(std::get<Block>(std::move(body_res)));

            Stmt stmt{
                .span = span_cover(kw.span, body->span),
                .node = WhileStmt{.cond = std::move(cond),
                                  .body = std::move(body),
                                  .invariants = std::move(invariants),
                                  .decreases = std::move(decreases)},
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

        // Expression statement (or assignment target when followed by '=').
        auto expr_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(expr_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(expr_res));
        }
        Expr expr = std::get<Expr>(std::move(expr_res));

        // Indexed element assignment: `arr[i] = expr;`. The expression parser
        // stops before the '=' (it is not an expression operator), leaving an
        // IndexExpr whose base is the array name. Only a plain NameExpr base
        // is accepted in the MVP (the target must be a local array binding).
        if (auto* idx = std::get_if<IndexExpr>(&expr.node);
            idx != nullptr && idx->base != nullptr)
        {
            const auto* base_name = std::get_if<NameExpr>(&idx->base->node);
            if (base_name != nullptr && check(TokenKind::Equal))
            {
                advance(); // consume '='

                auto rhs_res = parse_expr();
                if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
                }
                Expr value = std::get<Expr>(std::move(rhs_res));

                if (auto err = consume(TokenKind::Semicolon, "expected ';' after assignment");
                    err.has_value())
                {
                    return *err;
                }
                const Token semi = previous();

                // Span: cover from first token of statement to semicolon.
                const Token first = tokens_[start_pos];
                Stmt stmt{
                    .span = span_cover(first.span, semi.span),
                    .node = IndexAssignStmt{.name = base_name->name,
                                            .index = std::move(*idx->index),
                                            .value = std::move(value)},
                };
                return stmt;
            }
        }

        // Assignment statement: a bare name followed by '=' is `name = expr;`.
        // `=` is not an expression operator, so the expression parser stops
        // before it; only a plain NameExpr target is accepted (member/scoped
        // targets are out of the MVP scope). The target must name an existing
        // binding — the resolver/type-checker reject unknown names.
        if (const auto* name = std::get_if<NameExpr>(&expr.node);
            name != nullptr && check(TokenKind::Equal))
        {
            advance(); // consume '='

            auto rhs_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(rhs_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(rhs_res));
            }
            Expr value = std::get<Expr>(std::move(rhs_res));

            if (auto err = consume(TokenKind::Semicolon, "expected ';' after assignment");
                err.has_value())
            {
                return *err;
            }
            const Token semi = previous();

            // Span: cover from first token of statement to semicolon.
            const Token first = tokens_[start_pos];
            Stmt stmt{
                .span = span_cover(first.span, semi.span),
                .node = AssignStmt{.name = name->name, .value = std::move(value)},
            };
            return stmt;
        }

        // Expression statement
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

    // Bitwise or: lowest-precedence bitwise operator (just above logical `&&`).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_bit_or()
    {
        auto lhs_res = parse_bit_xor();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Pipe))
        {
            const Token op = previous();
            auto rhs_res = parse_bit_xor();
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

    // Bitwise xor: binds tighter than bitwise or, looser than bitwise and.
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_bit_xor()
    {
        auto lhs_res = parse_bit_and();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Caret))
        {
            const Token op = previous();
            auto rhs_res = parse_bit_and();
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

    // Bitwise and: binds tighter than xor, looser than equality (C precedence).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_bit_and()
    {
        auto lhs_res = parse_equality();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Amp))
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

    // Logical `&&` binds looser than all bitwise operators (C precedence:
    // || < && < | < ^ < & < ==), so it parses bitwise or as its operand.
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_and()
    {
        auto lhs_res = parse_bit_or();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::AndAnd))
        {
            const Token op = previous();
            auto rhs_res = parse_bit_or();
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
        auto lhs_res = parse_shift();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::Less) || match(TokenKind::LessEqual) || match(TokenKind::Greater) ||
               match(TokenKind::GreaterEqual))
        {
            const Token op = previous();
            auto rhs_res = parse_shift();
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

    // Shifts bind tighter than comparisons, looser than additive (C-like).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic> parse_shift()
    {
        auto lhs_res = parse_term();
        if (std::holds_alternative<curlee::diag::Diagnostic>(lhs_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(lhs_res));
        }
        Expr expr = std::get<Expr>(std::move(lhs_res));

        while (match(TokenKind::ShiftLeft) || match(TokenKind::ShiftRight))
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

        while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent))
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
        if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Tilde))
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
                        value.node = curlee::parser::IntExpr{.lexeme = std::string(lit.lexeme)};
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

            // Indexed array element read: `arr[i]`. The index is a general
            // expression; the type checker requires it to be Int and the
            // verifier discharges the bounds obligation. Chained `[i][j]`
            // parses here and is rejected as multi-dimensional by the type
            // checker (MVP scope, issue #278).
            if (match(TokenKind::LBracket))
            {
                auto index_res = parse_expr();
                if (std::holds_alternative<curlee::diag::Diagnostic>(index_res))
                {
                    return std::get<curlee::diag::Diagnostic>(std::move(index_res));
                }
                if (auto err = consume(TokenKind::RBracket, "expected ']' after array index");
                    err.has_value())
                {
                    return *err;
                }
                const Token rbracket = previous();
                Expr access;
                access.span = span_cover(expr.span, rbracket.span);
                access.node = IndexExpr{
                    .base = std::make_unique<Expr>(std::move(expr)),
                    .index = std::make_unique<Expr>(std::get<Expr>(std::move(index_res)))};
                expr = std::move(access);
                continue;
            }

            // Runtime-address physical memory reads: `phys_read_u8(addr)`,
            // `phys_read_u16(addr)`, `phys_read_u32(addr)`, `phys_read_u64(addr)`.
            // The address is a general Int/U64 expression (issue #279) — a
            // runtime value (the multiboot2 info base captured by the boot
            // stub), unlike `phys<U>(literal)` which requires a constant.
            // Parsed as a dedicated node so the builtin names cannot be
            // shadowed by user functions (resolver defense) and the address
            // child is carried explicitly.
            if (const auto* phys_name = std::get_if<NameExpr>(&expr.node);
                phys_name != nullptr &&
                curlee::parser::is_runtime_phys_read_builtin_name(phys_name->name) &&
                check(TokenKind::LParen))
            {
                return parse_runtime_phys_read_call(expr.span, *phys_name);
            }

            // Runtime-address physical memory writes: `phys_write_u8(addr, v)`,
            // `phys_write_u16(addr, v)`, `phys_write_u32(addr, v)`,
            // `phys_write_u64(addr, v)`. The address is a general Int/U64
            // expression (issue #285) and the value is the corresponding
            // unsigned width — the write counterpart of phys_read_* (#279),
            // parsed as a dedicated node so the builtin names cannot be
            // shadowed by user functions (resolver defense) and the address +
            // value children are carried explicitly.
            if (const auto* phys_name = std::get_if<NameExpr>(&expr.node);
                phys_name != nullptr &&
                curlee::parser::is_runtime_phys_write_builtin_name(phys_name->name) &&
                check(TokenKind::LParen))
            {
                return parse_runtime_phys_write_call(expr.span, *phys_name);
            }

            // Address-of a Curlee-owned fixed-size array (issue #286):
            // `addr_of(arr)` returns the physical address of the array's
            // storage as an Int — the DMA descriptor-filling primitive (the
            // vring descriptor's addr field / the virtqueue PFN register).
            // Parsed as a dedicated node so the builtin name cannot be
            // shadowed by user functions (resolver defense) and the target
            // child is carried explicitly.
            if (const auto* addr_name = std::get_if<NameExpr>(&expr.node);
                addr_name != nullptr && curlee::parser::is_addr_of_builtin_name(addr_name->name) &&
                check(TokenKind::LParen))
            {
                return parse_addr_of_call(expr.span, *addr_name);
            }

            // x86 port I/O builtins: `port_inb(0x3FD)`, `port_outw(0x1CE, v)`.
            // The port must be a compile-time constant literal; the optional
            // value (out* variants) is a general expression of the matching
            // width. Parsed as a dedicated node so the constant-port rule is
            // enforced here (mirroring `phys<U>(literal)`).
            if (const auto* port_name = std::get_if<NameExpr>(&expr.node);
                port_name != nullptr && curlee::parser::is_port_io_builtin_name(port_name->name) &&
                check(TokenKind::LParen))
            {
                return parse_port_io_call(expr.span, *port_name);
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

    // Parse a runtime-address physical memory read builtin call:
    // `phys_read_u8(addr)` / `phys_read_u16(addr)` / `phys_read_u32(addr)` /
    // `phys_read_u64(addr)`. The caller has verified the next token is `(`.
    // The address is a general expression (issue #279): a runtime Int/U64
    // value such as the boot-stub-captured multiboot2 info base plus a
    // mutable byte cursor — not required to be a compile-time literal (unlike
    // `phys<U>(literal)`).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic>
    parse_runtime_phys_read_call(const curlee::source::Span& start_span, const NameExpr& name)
    {
        advance(); // consume '('

        auto addr_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(addr_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(addr_res));
        }
        auto addr = std::make_unique<Expr>(std::get<Expr>(std::move(addr_res)));

        if (auto err = consume(TokenKind::RParen, "expected ')' after runtime phys read address");
            err.has_value())
        {
            return *err;
        }
        const Token rparen = previous();

        Expr expr;
        expr.span = span_cover(start_span, rparen.span);
        expr.node = RuntimePhysReadExpr{.op = name.name, .addr = std::move(addr)};
        return expr;
    }

    // Parse an address-of builtin call: `addr_of(arr)`. The caller has
    // verified the next token is `(`. The argument is a single expression
    // (the type checker requires it to resolve to a fixed-size array
    // binding); the builtin returns the array storage's physical address as
    // an Int (issue #286).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic>
    parse_addr_of_call(const curlee::source::Span& start_span, const NameExpr& name)
    {
        advance(); // consume '('

        auto target_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(target_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(target_res));
        }
        auto target = std::make_unique<Expr>(std::get<Expr>(std::move(target_res)));

        if (auto err = consume(TokenKind::RParen, "expected ')' after addr_of argument");
            err.has_value())
        {
            return *err;
        }
        const Token rparen = previous();

        Expr expr;
        expr.span = span_cover(start_span, rparen.span);
        expr.node = AddrOfExpr{.target = std::move(target)};
        (void)name;
        return expr;
    }

    // Parse a runtime-address physical memory write builtin call:
    // `phys_write_u8(addr, v)` / `phys_write_u16(addr, v)` /
    // `phys_write_u32(addr, v)` / `phys_write_u64(addr, v)`. The caller has
    // verified the next token is `(`. The address is a general Int/U64
    // expression (issue #285) and the value is the matching unsigned width
    // (an Int value adapts for unsigned widths, mirroring `Phys<T>.write()`),
    // mirroring the phys_read_* builtins (issue #279).
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic>
    parse_runtime_phys_write_call(const curlee::source::Span& start_span, const NameExpr& name)
    {
        advance(); // consume '('

        auto addr_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(addr_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(addr_res));
        }
        auto addr = std::make_unique<Expr>(std::get<Expr>(std::move(addr_res)));

        if (auto err = consume(TokenKind::Comma,
                               "expected ',' after runtime phys write address");
            err.has_value())
        {
            return *err;
        }

        auto value_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(value_res));
        }
        auto value = std::make_unique<Expr>(std::get<Expr>(std::move(value_res)));

        if (auto err = consume(TokenKind::RParen,
                               "expected ')' after runtime phys write value");
            err.has_value())
        {
            return *err;
        }
        const Token rparen = previous();

        Expr expr;
        expr.span = span_cover(start_span, rparen.span);
        expr.node = RuntimePhysWriteExpr{.op = name.name,
                                         .addr = std::move(addr),
                                         .value = std::move(value)};
        return expr;
    }

    // Parse an x86 port I/O builtin call: `port_inb(0x3FD)` or
    // `port_outb(0x3F8, v)`. The caller has verified the next token is `(`.
    [[nodiscard]] std::variant<Expr, curlee::diag::Diagnostic>
    parse_port_io_call(const curlee::source::Span& start_span, const NameExpr& name)
    {
        advance(); // consume '('

        // The port is a general Int expression (issue #276): a compile-time
        // constant literal (IntLiteral / PhysAddrLiteral, mirroring phys()
        // addresses) or a runtime expression such as a `let`-bound base plus a
        // constant offset (`io_base + 0x0E`, the virtio_net.c pattern). The
        // type checker requires the expression to be Int-typed; the verifier
        // lowers constant ports as before and runtime ports as opaque terms.
        auto port_res = parse_expr();
        if (std::holds_alternative<curlee::diag::Diagnostic>(port_res))
        {
            return std::get<curlee::diag::Diagnostic>(std::move(port_res));
        }
        auto port = std::make_unique<Expr>(std::get<Expr>(std::move(port_res)));

        std::unique_ptr<Expr> value;
        const bool is_out = name.name.rfind("port_out", 0) == 0;
        if (is_out)
        {
            if (auto err = consume(TokenKind::Comma, "expected ',' after port in " +
                                                         std::string(name.name) + "()");
                err.has_value())
            {
                return *err;
            }
            // The value is a general expression of the matching width. Hex /
            // underscore constants (PhysAddrLiteral) parse as constant Int
            // expressions via parse_primary (mirrors the write() value handling).
            auto value_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(value_res));
            }
            value = std::make_unique<Expr>(std::get<Expr>(std::move(value_res)));
        }

        if (auto err = consume(TokenKind::RParen, "expected ')' after port I/O arguments");
            err.has_value())
        {
            return *err;
        }
        const Token rparen = previous();

        Expr expr;
        expr.span = span_cover(start_span, rparen.span);
        expr.node = PortIOExpr{.op = name.name,
                               .port = std::move(port),
                               .value = std::move(value)};
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
            expr.node = IntExpr{.lexeme = std::string(lit.lexeme)};
            return expr;
        }

        // Hex/underscore integer literals (PhysAddrLiteral) are wrapped as
        // constant Int expressions so they can appear in general expression
        // positions — e.g. the constant offset of a runtime port
        // (`io_base + 0x0E`, issue #276) or a write()/port_out* value. This
        // mirrors how the port I/O value parser historically consumed them.
        if (match(TokenKind::PhysAddrLiteral))
        {
            const Token lit = previous();
            Expr expr;
            expr.span = lit.span;
            expr.node = IntExpr{.lexeme = std::string(lit.lexeme)};
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

        // Fixed-size array repeat literal: `[v; N]` (issue #278). The value is
        // a general expression and N is a compile-time constant: an integer
        // literal or, with `--define` build constants (issue #296), a constant
        // expression over defines and literals (`[0; W * H]`). Only valid as a
        // `let`/`static` initializer for a `[T; N]` binding.
        if (match(TokenKind::LBracket))
        {
            const Token lbracket = previous();
            auto value_res = parse_expr();
            if (std::holds_alternative<curlee::diag::Diagnostic>(value_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(value_res));
            }
            if (auto err = consume(TokenKind::Semicolon, "expected ';' after array literal value");
                err.has_value())
            {
                return *err;
            }
            auto count_res = parse_array_length_constant();
            if (std::holds_alternative<curlee::diag::Diagnostic>(count_res))
            {
                return std::get<curlee::diag::Diagnostic>(std::move(count_res));
            }
            const std::string count = std::get<std::string>(std::move(count_res));
            if (auto err = consume(TokenKind::RBracket, "expected ']' after array literal count");
                err.has_value())
            {
                return *err;
            }
            const Token rbracket = previous();
            Expr expr;
            expr.span = span_cover(lbracket.span, rbracket.span);
            expr.node = ArrayLiteralExpr{
                .value = std::make_unique<Expr>(std::get<Expr>(std::move(value_res))),
                .count_lexeme = std::move(count)};
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

            // `--define` build constant (issue #296): a bare identifier that
            // matches a provided define name lowers to an IntExpr holding the
            // constant's decimal value — the per-build-target discriminator
            // (`if (JOE_PVH_BOOT == 1)`, `return ASSET_REGION_W;`) and the
            // value source for array sizes. Substitution is limited to BARE
            // primary position: a define name followed by `(`, `[` or `.` is a
            // user collision with a function/index/member name and is left to
            // the resolver (so a call on a substituted constant never reaches
            // the type checker).
            if (!check(TokenKind::LParen) && !check(TokenKind::LBracket) &&
                !check(TokenKind::Dot))
            {
                if (const auto define = lookup_define(name.lexeme); define.has_value())
                {
                    Expr expr;
                    expr.span = name.span;
                    expr.node = IntExpr{.lexeme = std::to_string(*define)};
                    return expr;
                }
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
            if (auto err = consume(TokenKind::Less, "expected '<' after 'phys'"); err.has_value())
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
            expr.node =
                curlee::parser::PhysExpr{.element_kind = elem.lexeme, .lexeme = addr.lexeme};
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
        const bool has_decls = has_types || !p.statics.empty();
        if (!p.imports.empty() && (has_decls || !p.functions.empty()))
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

        if (has_types && !p.statics.empty())
        {
            out_ << "\n";
        }

        for (std::size_t i = 0; i < p.statics.size(); ++i)
        {
            dump_static_decl(p.statics[i]);
            if (i + 1 < p.statics.size())
            {
                out_ << "\n";
            }
        }

        if ((has_types || !p.statics.empty()) && !p.functions.empty())
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
        if (t.is_array())
        {
            out_ << "[" << t.name << "; " << *t.array_len << "]";
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

    void dump_static_decl(const StaticVarDecl& s)
    {
        if (s.is_extern)
        {
            out_ << "extern ";
        }
        out_ << "static " << s.name << ": ";
        dump_type(s.type);
        out_ << " = ";
        dump_expr(s.value);
        out_ << ";\n";
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

        if (!f.requires_clauses.empty() || !f.ensures.empty() || !f.ghost_lets.empty() ||
            f.fuel_bound.has_value())
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
            if (f.fuel_bound.has_value())
            {
                out_ << " fuel ";
                dump_pred(*f.fuel_bound);
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

    void dump_stmt_node(const AssignStmt& s)
    {
        out_ << s.name << " = ";
        dump_expr(s.value);
        out_ << ";";
    }

    void dump_stmt_node(const IndexAssignStmt& s)
    {
        out_ << s.name << "[";
        dump_expr(s.index);
        out_ << "] = ";
        dump_expr(s.value);
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
        if (!s.invariants.empty() || s.decreases.has_value())
        {
            out_ << "[ ";
            for (const auto& inv : s.invariants)
            {
                out_ << "invariant ";
                dump_pred(inv);
                out_ << "; ";
            }
            if (s.decreases.has_value())
            {
                out_ << "decreases ";
                dump_pred(*s.decreases);
                out_ << "; ";
            }
            out_ << "] ";
        }
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

    void dump_expr_node(const IndexExpr& e)
    {
        dump_expr(*e.base);
        out_ << "[";
        dump_expr(*e.index);
        out_ << "]";
    }

    void dump_expr_node(const ArrayLiteralExpr& e)
    {
        out_ << "[";
        dump_expr(*e.value);
        out_ << "; " << e.count_lexeme << "]";
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

    void dump_expr_node(const PortIOExpr& e)
    {
        out_ << e.op << "(";
        dump_expr(*e.port);
        if (e.value != nullptr)
        {
            out_ << ", ";
            dump_expr(*e.value);
        }
        out_ << ")";
    }

    void dump_expr_node(const RuntimePhysReadExpr& e)
    {
        out_ << e.op << "(";
        dump_expr(*e.addr);
        out_ << ")";
    }

    void dump_expr_node(const RuntimePhysWriteExpr& e)
    {
        out_ << e.op << "(";
        dump_expr(*e.addr);
        out_ << ", ";
        dump_expr(*e.value);
        out_ << ")";
    }

    void dump_expr_node(const AddrOfExpr& e)
    {
        out_ << "addr_of(";
        dump_expr(*e.target);
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

ParseResult parse(std::span<const curlee::lexer::Token> tokens,
                  std::span<const BuildDefine> defines)
{
    auto result = Parser(tokens, defines).parse_program();
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
