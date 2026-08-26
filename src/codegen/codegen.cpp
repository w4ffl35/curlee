// SPDX-License-Identifier: MIT
#include <curlee/codegen/codegen.h>
#include <curlee/lexer/token.h>
#include <curlee/resolver/resolver.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace curlee::codegen
{
namespace
{

using curlee::diag::Diagnostic;
using curlee::diag::Severity;
using curlee::parser::ArrayLiteralExpr;
using curlee::parser::AssignStmt;
using curlee::parser::BinaryExpr;
using curlee::parser::BlockStmt;
using curlee::parser::BoolExpr;
using curlee::parser::CallExpr;
using curlee::parser::EnumDecl;
using curlee::parser::Expr;
using curlee::parser::ExprStmt;
using curlee::parser::Function;
using curlee::parser::GroupExpr;
using curlee::parser::GhostLetStmt;
using curlee::parser::IfStmt;
using curlee::parser::IndexAssignStmt;
using curlee::parser::IndexExpr;
using curlee::parser::IntExpr;
using curlee::parser::LetStmt;
using curlee::parser::MatchStmt;
using curlee::parser::MemberExpr;
using curlee::parser::NameExpr;
using curlee::parser::PhysExpr;
using curlee::parser::PhysReadExpr;
using curlee::parser::PhysWriteExpr;
using curlee::parser::PortIOExpr;
using curlee::parser::Program;
using curlee::parser::RuntimePhysReadExpr;
using curlee::parser::ReturnStmt;
using curlee::parser::ScopedNameExpr;
using curlee::parser::Stmt;
using curlee::parser::StringExpr;
using curlee::parser::StructDecl;
using curlee::parser::StructLiteralExpr;
using curlee::parser::UnaryExpr;
using curlee::parser::UnsafeStmt;
using curlee::parser::WhileStmt;
using curlee::source::Span;

Diagnostic error_at(Span span, std::string message)
{
    Diagnostic d;
    d.severity = Severity::Error;
    d.message = std::move(message);
    d.span = span;
    return d;
}

// A diagnostic produced while emitting the current function. Attaches the
// function name so the CLI can resolve the owning source file for spans that
// collide across imported files (spans are per-file byte offsets).
Diagnostic error_at_function(std::string_view function_name, Span span, std::string message)
{
    Diagnostic d = error_at(span, std::move(message));
    d.function_name = std::string(function_name);
    return d;
}

// ---------------------------------------------------------------------------
// Name mangling
// ---------------------------------------------------------------------------

// Top-level Curlee names are prefixed with `curlee_` so they cannot collide
// with runtime/ABI symbols (the freestanding runtime and crt0). The `main`
// entry point is emitted as `curlee_main`.
std::string mangle_name(std::string_view name)
{
    return "curlee_" + std::string(name);
}

std::string mangle_type_name(std::string_view name)
{
    return "curlee_" + std::string(name);
}

std::string mangle_variant_name(std::string_view enum_name, std::string_view variant_name)
{
    return "curlee_" + std::string(enum_name) + "_" + std::string(variant_name);
}

// Phys address literals are lexed with underscores preserved (e.g. 0xFD00_0000),
// but underscores are not valid in integer literals until C23. Strip them for
// the emitted C, which targets C11/C17 freestanding. This applies to any
// integer lexeme that may carry underscores (phys addresses and phys write
// values wrapped as IntExpr by the parser); ordinary decimal Int literals have
// no underscores, so stripping is a no-op for them.
std::string strip_underscores(std::string_view lexeme)
{
    std::string out;
    out.reserve(lexeme.size());
    for (const char ch : lexeme)
    {
        if (ch != '_')
        {
            out.push_back(ch);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Port I/O usage scan
// ---------------------------------------------------------------------------
// The x86 port I/O helpers (curlee_port_inb/...) are emitted into the prologue
// only when the program actually uses port I/O, keeping unrelated golden output
// byte-identical. These walkers detect that usage before the prologue is
// written.

bool expr_uses_port_io(const Expr& e);

bool stmt_uses_port_io(const Stmt& s);

bool block_uses_port_io(const curlee::parser::Block& b)
{
    for (const auto& stmt : b.stmts)
    {
        if (stmt_uses_port_io(stmt))
        {
            return true;
        }
    }
    return false;
}

bool stmt_uses_port_io(const Stmt& s)
{
    return std::visit(
        [](const auto& node) -> bool
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, LetStmt>)
            {
                return expr_uses_port_io(node.value);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::GhostLetStmt>)
            {
                return expr_uses_port_io(node.value);
            }
            else if constexpr (std::is_same_v<Node, IndexAssignStmt>)
            {
                return expr_uses_port_io(node.index) || expr_uses_port_io(node.value);
            }
            else if constexpr (std::is_same_v<Node, ReturnStmt>)
            {
                return node.value.has_value() && expr_uses_port_io(*node.value);
            }
            else if constexpr (std::is_same_v<Node, ExprStmt>)
            {
                return expr_uses_port_io(node.expr);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::BlockStmt>)
            {
                return node.block != nullptr && block_uses_port_io(*node.block);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::UnsafeStmt>)
            {
                return node.body != nullptr && block_uses_port_io(*node.body);
            }
            else if constexpr (std::is_same_v<Node, IfStmt>)
            {
                if (expr_uses_port_io(node.cond))
                {
                    return true;
                }
                if (node.then_block != nullptr && block_uses_port_io(*node.then_block))
                {
                    return true;
                }
                return node.else_block != nullptr && block_uses_port_io(*node.else_block);
            }
            else if constexpr (std::is_same_v<Node, WhileStmt>)
            {
                if (expr_uses_port_io(node.cond))
                {
                    return true;
                }
                return node.body != nullptr && block_uses_port_io(*node.body);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::MatchStmt>)
            {
                if (expr_uses_port_io(node.value))
                {
                    return true;
                }
                for (const auto& arm : node.arms)
                {
                    if (arm.body != nullptr && block_uses_port_io(*arm.body))
                    {
                        return true;
                    }
                }
                return false;
            }
            return false;
        },
        s.node);
}

bool expr_uses_port_io(const Expr& e)
{
    return std::visit(
        [](const auto& node) -> bool
        {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, PortIOExpr>)
            {
                return true;
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::UnaryExpr>)
            {
                return node.rhs != nullptr && expr_uses_port_io(*node.rhs);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::BinaryExpr>)
            {
                return (node.lhs != nullptr && expr_uses_port_io(*node.lhs)) ||
                       (node.rhs != nullptr && expr_uses_port_io(*node.rhs));
            }
            else if constexpr (std::is_same_v<Node, CallExpr>)
            {
                if (node.callee != nullptr && expr_uses_port_io(*node.callee))
                {
                    return true;
                }
                for (const auto& arg : node.args)
                {
                    if (expr_uses_port_io(arg))
                    {
                        return true;
                    }
                }
                return false;
            }
            else if constexpr (std::is_same_v<Node, MemberExpr>)
            {
                return node.base != nullptr && expr_uses_port_io(*node.base);
            }
            else if constexpr (std::is_same_v<Node, IndexExpr>)
            {
                return node.index != nullptr && expr_uses_port_io(*node.index);
            }
            else if constexpr (std::is_same_v<Node, ArrayLiteralExpr>)
            {
                return node.value != nullptr && expr_uses_port_io(*node.value);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::GroupExpr>)
            {
                return node.inner != nullptr && expr_uses_port_io(*node.inner);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::StructLiteralExpr>)
            {
                for (const auto& f : node.fields)
                {
                    if (f.value != nullptr && expr_uses_port_io(*f.value))
                    {
                        return true;
                    }
                }
                return false;
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PhysReadExpr>)
            {
                return node.base != nullptr && expr_uses_port_io(*node.base);
            }
            else if constexpr (std::is_same_v<Node, curlee::parser::PhysWriteExpr>)
            {
                return (node.base != nullptr && expr_uses_port_io(*node.base)) ||
                       (node.value != nullptr && expr_uses_port_io(*node.value));
            }
            else if constexpr (std::is_same_v<Node, RuntimePhysReadExpr>)
            {
                // A runtime phys read is not itself port I/O, but its address
                // expression is general (issue #279) and could nest port I/O;
                // scan it so the prologue helpers are still emitted.
                return node.addr != nullptr && expr_uses_port_io(*node.addr);
            }
            return false;
        },
        e.node);
}

bool program_uses_port_io(const curlee::parser::Program& program)
{
    for (const auto& f : program.functions)
    {
        if (block_uses_port_io(f.body))
        {
            return true;
        }
        for (const auto& g : f.ghost_lets)
        {
            if (expr_uses_port_io(g.value))
            {
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Type mapping
// ---------------------------------------------------------------------------

std::optional<std::string_view> c_type_for_core(std::string_view name)
{
    if (name == "Int")
    {
        return std::string_view("int64_t");
    }
    if (name == "Bool")
    {
        return std::string_view("_Bool");
    }
    if (name == "U8")
    {
        return std::string_view("uint8_t");
    }
    if (name == "U16")
    {
        return std::string_view("uint16_t");
    }
    if (name == "U32")
    {
        return std::string_view("uint32_t");
    }
    if (name == "U64")
    {
        return std::string_view("uint64_t");
    }
    if (name == "Unit")
    {
        return std::string_view("void");
    }
    return std::nullopt;
}

std::string_view c_type_for_phys_element(std::string_view element_kind)
{
    if (element_kind == "U8")
    {
        return std::string_view("uint8_t");
    }
    if (element_kind == "U16")
    {
        return std::string_view("uint16_t");
    }
    if (element_kind == "U32")
    {
        return std::string_view("uint32_t");
    }
    if (element_kind == "U64")
    {
        return std::string_view("uint64_t");
    }
    // Defensive fallback (the front-end restricts to U8/U16/U32/U64).
    return std::string_view("uint32_t");
}

// ---------------------------------------------------------------------------
// Output buffer with indentation
// ---------------------------------------------------------------------------

class CWriter
{
  public:
    void newline() { out_.push_back('\n'); }

    void line(const std::string& text)
    {
        indent();
        out_.append(text);
        out_.push_back('\n');
    }

    void open_brace()
    {
        indent();
        out_.append("{\n");
        ++depth_;
    }

    void close_brace()
    {
        --depth_;
        indent();
        out_.append("}\n");
    }

    void indent()
    {
        for (int i = 0; i < depth_; ++i)
        {
            out_.append("    ");
        }
    }

    [[nodiscard]] const std::string& str() const { return out_; }

  private:
    std::string out_;
    int depth_ = 0;
};

// ---------------------------------------------------------------------------
// Freestanding C emitter
// ---------------------------------------------------------------------------

class CEmitter
{
  public:
    CodegenResult run(const Program& program)
    {
        program_ = &program;

        collect_decls();

        // Defense-in-depth: the front-end rejects user declarations of builtin
        // names (resolver/type-checker), but a directly-constructed Program
        // must not silently pass. This mirrors the front-end guard.
        for (const auto& f : program.functions)
        {
            if (curlee::resolver::is_builtin_call_name(f.name))
            {
                diags_.push_back(
                    error_at(f.span, "cannot declare builtin function '" + std::string(f.name) +
                                         "'"));
                return diags_;
            }
        }

        // Ghost functions are verification-only and erased from the freestanding
        // C output. They cannot be `main` (the entry point must be runnable).
        for (const auto& f : program.functions)
        {
            if (f.is_ghost && f.name == "main")
            {
                diags_.push_back(error_at(f.span, "ghost function cannot be 'main'"));
                return diags_;
            }
        }

        const Function* entry = find_main(program);
        if (entry == nullptr)
        {
            diags_.push_back(error_at({}, "no entry function 'main' found"));
            return diags_;
        }

        // Pre-pass: reject unsupported parameter and return types for ALL
        // functions before emitting anything. This ensures no partial (and
        // possibly malformed) C text is produced when a signature is invalid —
        // e.g. a String parameter would otherwise leak a `void s` into the
        // forward-declaration block before the body-time rejection fired.
        validate_signatures(program);
        if (!diags_.empty())
        {
            return diags_;
        }

        // The port I/O helpers are emitted only when the program uses them,
        // keeping unrelated golden output byte-identical.
        const bool uses_port_io = program_uses_port_io(program);
        emit_prologue(uses_port_io);

        // Forward declarations for all nominal types so that struct and enum
        // definitions can reference each other in any order (the type checker
        // allows forward references). These follow the banner + #include.
        for (const auto& s : program.structs)
        {
            writer_.line("typedef struct " + mangle_type_name(s.name) + " " +
                         mangle_type_name(s.name) + ";");
        }
        for (const auto& e : program.enums)
        {
            writer_.line("typedef struct " + mangle_type_name(e.name) + " " +
                         mangle_type_name(e.name) + ";");
        }
        if (!program.structs.empty() || !program.enums.empty())
        {
            writer_.newline();
        }

        // Type definitions in dependency order so that a struct field or enum
        // payload referencing another nominal type by value is always complete
        // at the point of use (the forward typedefs above are only enough for
        // pointers; by-value fields need the complete type).
        if (!emit_types_in_dependency_order())
        {
            return diags_;
        }

        // Forward declarations for all functions (C requires declaration
        // before use; no implicit declarations in strict C).
        emit_forward_decls();

        // Function definitions in declaration order (deterministic golden output).
        // Extern functions have no definition: they were already declared via
        // emit_forward_decls (extern ... ;) and the symbol resolves at link time.
        // Ghost functions are verification-only and erased from the C output.
        for (const auto& f : program.functions)
        {
            if (f.is_extern)
            {
                continue;
            }
            if (f.is_ghost)
            {
                continue;
            }
            emit_function(f, f.name == "main");
            if (!diags_.empty())
            {
                return diags_;
            }
        }

        return writer_.str();
    }

  private:
    const Program* program_ = nullptr;
    CWriter writer_;
    std::vector<Diagnostic> diags_;

    std::unordered_map<std::string_view, const StructDecl*> structs_;
    std::unordered_map<std::string_view, const EnumDecl*> enums_;
    std::unordered_set<std::string_view> function_names_;

    // Phys<T> variables in the current function: name -> element kind.
    // A Phys value is either a literal (phys<U32>(0xADDR)) whose address is
    // embedded at the read()/write() site, or a parameter (`uintN_t*`) whose
    // read()/write() dereferences the pointer parameter.
    struct PhysVar
    {
        std::string_view element_kind;
        // Literal address text when bound from phys<U>(addr); empty when the
        // value is a Phys parameter (dereference the parameter pointer).
        std::string addr_text;
    };

    std::unordered_map<std::string_view, PhysVar> phys_vars_;

    // C return type of the function currently being emitted ("" outside a
    // function). Used to guard against emitting `return <expr>;` from a
    // void-mapped return type.
    std::string current_return_c_type_;

    // Name of the function currently being emitted ("" outside a function).
    // Attached to diagnostics so the CLI can resolve the owning source file
    // for spans that collide across imported files.
    std::string current_function_name_;

    // A type is storable in C if it lowers to a real C type (not `void`).
    // Capabilities, Phys<T>, Unit, String, Vec, Set all lower to `void` and
    // cannot appear in a storage position (struct field / enum payload / let).
    // Fixed-size arrays (`[T; N]`) are storable ONLY as local `let` bindings
    // (issue #278): struct fields, enum payloads, parameters and return types
    // reject them (type_is_storable returns false for arrays so those call
    // sites emit the standard rejection diagnostic with the array-specific
    // note below).
    bool type_is_storable(const curlee::parser::TypeName& type_name) const
    {
        if (type_name.is_array())
        {
            return false;
        }
        if (type_name.is_capability)
        {
            return false;
        }
        if (type_name.name == "Phys")
        {
            return false;
        }
        if (c_type_for_core(type_name.name).has_value())
        {
            // Unit maps to void; not storable.
            return type_name.name != "Unit";
        }
        if (structs_.contains(type_name.name) || enums_.contains(type_name.name))
        {
            return true;
        }
        // String/Vec/Set/unknown: not storable.
        return false;
    }

    // Unsupported types that silently fall through to void would emit invalid C
    // (e.g. a String parameter becomes `void s`, a Unit struct field becomes an
    // empty struct). We collect a diagnostic instead.
    void reject_unsupported_type(const curlee::parser::TypeName& type_name, Span span,
                                 std::string_view context)
    {
        if (type_is_storable(type_name))
        {
            return;
        }
        if (type_name.is_array())
        {
            diags_.push_back(error_at(
                span, std::string(context) +
                          ": array types are only supported as freestanding local "
                          "'let' bindings in the MVP ([T; N]; indexed element access)"));
            return;
        }
        std::string message = std::string(context) + ": type '" +
                              std::string(type_name.name) +
                              "' is not supported in freestanding target";
        if (type_name.name == "Phys")
        {
            message += " (Phys<T> has no storable C representation; use it only "
                       "as a local or parameter for read()/write())";
        }
        else if (type_name.name == "Unit")
        {
            message += " (Unit has no storable C representation)";
        }
        else if (type_name.is_capability)
        {
            message += " (capabilities have no runtime value)";
        }
        diags_.push_back(error_at(span, message));
    }

    // Push a diagnostic tagged with the function currently being emitted, so
    // the CLI can resolve the owning source file for spans that collide across
    // imported files (spans are per-file byte offsets).
    void push_error(Span span, std::string message)
    {
        diags_.push_back(
            error_at_function(current_function_name_, span, std::move(message)));
    }

    void collect_decls()
    {
        for (const auto& s : program_->structs)
        {
            structs_.emplace(s.name, &s);
        }
        for (const auto& e : program_->enums)
        {
            enums_.emplace(e.name, &e);
        }
        for (const auto& f : program_->functions)
        {
            function_names_.insert(f.name);
        }
    }

    static const Function* find_main(const Program& program)
    {
        for (const auto& f : program.functions)
        {
            if (f.name == "main")
            {
                return &f;
            }
        }
        return nullptr;
    }

    // Reject function signatures whose parameter or return types have no valid
    // C representation on the freestanding target. Runs before ANY emission so
    // no partial/malformed C is produced.
    void validate_signatures(const Program& program)
    {
        for (const auto& f : program.functions)
        {
            // Ghost functions are verification-only and erased from the C output;
            // their signatures are never emitted, so skip validation.
            if (f.is_ghost)
            {
                continue;
            }
            // Parameters: Phys<T> is fine (uintN_t*); capabilities are dropped;
            // Unit/String/Vec/Set are rejected.
            for (const auto& p : f.params)
            {
                if (p.type.is_capability || p.type.name == "Phys")
                {
                    continue;
                }
                if (!type_is_storable(p.type))
                {
                    diags_.push_back(error_at_function(
                        f.name, p.span,
                        "parameter '" + std::string(p.name) +
                            "' of function '" + std::string(f.name) +
                            "': type '" + std::string(p.type.name) +
                            "' is not supported in freestanding target"));
                }
            }

            // Return type: Phys<T> lowers to void and cannot carry a returned
            // value, so it is rejected. Unit is fine (it lowers to void; the
            // function ends with a bare `return;` or falls off the end).
            // String/Vec/Set returns are rejected as non-storable.
            if (f.return_type.has_value())
            {
                const auto& rt = *f.return_type;
                if (rt.name == "Phys")
                {
                    diags_.push_back(error_at_function(
                        f.name, f.span,
                        "function '" + std::string(f.name) +
                            "' has return type 'Phys' which cannot be returned "
                            "in freestanding C (Phys<T> has no storable "
                            "representation)"));
                }
                else if (rt.name == "Unit")
                {
                    // Allowed: `void` return.
                }
                else if (!type_is_storable(rt))
                {
                    diags_.push_back(error_at_function(
                        f.name, f.span,
                        "return type of function '" + std::string(f.name) +
                            "': type '" + std::string(rt.name) +
                            "' is not supported in freestanding target"));
                }
            }
        }
    }

    void emit_prologue(bool uses_port_io)
    {
        writer_.line("/* Generated by curlee build --target freestanding-c. */");
        writer_.line("/* Verified Curlee program compiled to freestanding C. */");
        writer_.newline();
        writer_.line("#include <stdint.h>");
        writer_.newline();
        if (uses_port_io)
        {
            emit_port_io_helpers();
        }
    }

    // Emit the x86 port I/O builtins as `static inline` helpers wrapping the
    // `in`/`out` instructions. Helpers (rather than raw `__asm__` at each call
    // site) keep the emitted C valid under -std=c11: a value-returning `in`
    // cannot be expressed as an inline-asm expression without GNU statement
    // expressions. The "Nd" constraint uses an 8-bit immediate for ports in
    // 0..255 and the DX register otherwise, so constant ports stay constant
    // while the emitted call sites remain plain C function calls.
    void emit_port_io_helpers()
    {
        writer_.line("/* x86 port I/O builtins (constant or let-bound-base + constant-offset ports). */");
        writer_.line("static inline uint8_t curlee_port_inb(uint16_t port)");
        writer_.open_brace();
        writer_.line("uint8_t v;");
        writer_.line("__asm__ volatile(\"inb %1, %0\" : \"=a\"(v) : \"Nd\"(port));");
        writer_.line("return v;");
        writer_.close_brace();
        writer_.newline();
        writer_.line("static inline uint16_t curlee_port_inw(uint16_t port)");
        writer_.open_brace();
        writer_.line("uint16_t v;");
        writer_.line("__asm__ volatile(\"inw %1, %0\" : \"=a\"(v) : \"Nd\"(port));");
        writer_.line("return v;");
        writer_.close_brace();
        writer_.newline();
        writer_.line("static inline uint32_t curlee_port_inl(uint16_t port)");
        writer_.open_brace();
        writer_.line("uint32_t v;");
        writer_.line("__asm__ volatile(\"inl %1, %0\" : \"=a\"(v) : \"Nd\"(port));");
        writer_.line("return v;");
        writer_.close_brace();
        writer_.newline();
        writer_.line("static inline void curlee_port_outb(uint16_t port, uint8_t value)");
        writer_.open_brace();
        writer_.line("__asm__ volatile(\"outb %0, %1\" ::\"a\"(value), \"Nd\"(port));");
        writer_.close_brace();
        writer_.newline();
        writer_.line("static inline void curlee_port_outw(uint16_t port, uint16_t value)");
        writer_.open_brace();
        writer_.line("__asm__ volatile(\"outw %0, %1\" ::\"a\"(value), \"Nd\"(port));");
        writer_.close_brace();
        writer_.newline();
        writer_.line("static inline void curlee_port_outl(uint16_t port, uint32_t value)");
        writer_.open_brace();
        writer_.line("__asm__ volatile(\"outl %0, %1\" ::\"a\"(value), \"Nd\"(port));");
        writer_.close_brace();
        writer_.newline();
    }

    // Emit forward declarations for every internal function so calls can
    // reference functions defined later in the translation unit (C requires a
    // declaration before use; no implicit declarations in strict C).
    void emit_forward_decls()
    {
        for (const auto& f : program_->functions)
        {
            // Ghost functions are verification-only and erased from the C
            // output: no forward declaration is emitted.
            if (f.is_ghost)
            {
                continue;
            }
            const std::string ret = return_c_type(f);
            if (f.is_extern)
            {
                // Extern functions are provided by the host at link time. Emit
                // a plain `extern` C declaration with the identifier verbatim
                // (no curlee_ mangle, no `static`): the symbol must match the
                // boot stub / host implementation exactly.
                writer_.line("extern " + ret + " " + std::string(f.name) + "(" +
                             params_c_list(f) + ");");
                continue;
            }

            const std::string name = f.name == "main" ? "curlee_main" : mangle_name(f.name);
            // Internal functions get static linkage; the declaration must match
            // the definition's linkage (static declaration after a non-static
            // declaration is an error in C).
            writer_.line((f.name == "main" ? "" : "static ") + ret + " " + name + "(" +
                         params_c_list(f) + ");");
        }
        writer_.newline();
    }

    // -----------------------------------------------------------------------
    // Type resolution
    // -----------------------------------------------------------------------

    std::string c_type_for_type_name(const curlee::parser::TypeName& type_name)
    {
        if (type_name.is_capability)
        {
            // Capabilities are represented as Unit at runtime; authority is
            // gated by the host at the link/run boundary. They are dropped from
            // emitted parameter lists (see params_c_list).
            return "void";
        }

        if (type_name.is_array())
        {
            // Fixed-size arrays lower to `T name[N];`. This returns the ELEMENT
            // C type; the `[N]` declarator and the initializer are emitted by
            // the array-let path in emit_stmt_node(const LetStmt&). Struct
            // fields/params/returns reject arrays before reaching here.
            return c_type_for_type_name(
                curlee::parser::TypeName{.span = type_name.span,
                                         .is_capability = false,
                                         .name = type_name.name,
                                         .type_arg = std::nullopt,
                                         .array_len = std::nullopt});
        }

        if (const auto core = c_type_for_core(type_name.name); core.has_value())
        {
            return std::string(*core);
        }

        if (type_name.name == "Phys")
        {
            // Phys<T> values lower to a pointer of the element type at
            // read()/write() sites; no scalar storage type is needed for a
            // literal binding (the binding is a comment).
            return "void";
        }

        if (structs_.contains(type_name.name))
        {
            return mangle_type_name(type_name.name);
        }

        if (enums_.contains(type_name.name))
        {
            return mangle_type_name(type_name.name);
        }

        // Unsupported nominal types (String/Vec/Set...). reject_unsupported_type
        // fires a diagnostic before this is ever used to emit a declaration.
        return "void";
    }

    // -----------------------------------------------------------------------
    // Type declarations
    // -----------------------------------------------------------------------

    // Emit a complete placeholder definition for a nominal type whose
    // fields/payloads were all rejected. The `int _unused;` member keeps the
    // struct non-empty (C forbids empty structs) and makes the type complete,
    // so other definitions referencing it by value stay well-formed. The
    // accompanying diagnostics are what actually fail the build; this only
    // keeps the (discarded) text from leaking invalid C.
    void emit_placeholder_struct(std::string_view name)
    {
        writer_.line("typedef struct " + mangle_type_name(name) + " {");
        writer_.line("    int _unused;");
        writer_.line("} " + mangle_type_name(name) + ";");
        writer_.newline();
    }

    void emit_struct_decl(const StructDecl& s)
    {
        writer_.line("typedef struct " + mangle_type_name(s.name) + " {");
        // A struct with zero emitted members is invalid C (GCC's empty-struct
        // extension only), so a field of a non-storable type is a hard error
        // rather than a silently-omitted member.
        bool emitted_any = false;
        for (const auto& f : s.fields)
        {
            if (!type_is_storable(f.type))
            {
                reject_unsupported_type(f.type, f.span, "struct field '" +
                                                              std::string(s.name) + "." +
                                                              std::string(f.name) + "'");
                continue;
            }
            const std::string ft = c_type_for_type_name(f.type);
            writer_.line("    " + ft + " " + std::string(f.name) + ";");
            emitted_any = true;
        }
        if (!emitted_any)
        {
            // All fields rejected (diagnostics pushed): emit a complete
            // placeholder so other types referencing this one by value remain
            // valid C.
            emit_placeholder_struct(s.name);
            return;
        }
        writer_.line("} " + mangle_type_name(s.name) + ";");
        writer_.newline();
    }

    // Enums are lowered to a struct with a tag field and, when any variant
    // carries a payload, a payload union. Variant tags are emitted as enum
    // constants. When no variant has a payload the union is omitted entirely
    // (an empty union is invalid C).
    void emit_enum_decl(const EnumDecl& e)
    {
        writer_.line("enum " + mangle_type_name(e.name) + "_tag {");
        for (std::size_t i = 0; i < e.variants.size(); ++i)
        {
            writer_.line("    " + mangle_variant_name(e.name, e.variants[i].name) + " = " +
                         std::to_string(i) + ",");
        }
        writer_.line("};");
        writer_.newline();

        // Payload types that lower to `void` (String, Phys<T>, Unit, capability)
        // are rejected: the union must contain exactly the supported payload
        // members, and an empty union is invalid C. Reject before deciding
        // whether to emit the union so no empty-union case is reachable.
        std::vector<const curlee::parser::TypeName*> storable_payloads;
        bool rejected_payload = false;
        for (const auto& v : e.variants)
        {
            if (!v.payload.has_value())
            {
                continue;
            }
            if (!type_is_storable(*v.payload))
            {
                rejected_payload = true;
                reject_unsupported_type(*v.payload, v.span, "enum payload '" +
                                                                 std::string(e.name) + "." +
                                                                 std::string(v.name) + "'");
                continue;
            }
            storable_payloads.push_back(&*v.payload);
        }
        if (rejected_payload)
        {
            // Keep the emitted C well-formed even though diagnostics will
            // discard it: emit a complete placeholder so no empty-struct /
            // empty-union extension leaks through.
            emit_placeholder_struct(e.name);
            return;
        }

        writer_.line("typedef struct " + mangle_type_name(e.name) + " {");
        writer_.line("    enum " + mangle_type_name(e.name) + "_tag tag;");
        if (!storable_payloads.empty())
        {
            writer_.line("    union {");
            for (const auto& v : e.variants)
            {
                if (v.payload.has_value() && type_is_storable(*v.payload))
                {
                    writer_.line("        " + c_type_for_type_name(*v.payload) + " " +
                                 std::string(v.name) + ";");
                }
            }
            writer_.line("    } payload;");
        }
        writer_.line("} " + mangle_type_name(e.name) + ";");
        writer_.newline();
    }

    // Nominal types referenced by value by a struct field or enum payload.
    // Returns the set of nominal type names `type_name` depends on (those it
    // must come after in the emitted C so its fields/payloads are complete).
    std::unordered_set<std::string> nominal_dependencies(
        const curlee::parser::TypeName& type_name) const
    {
        std::unordered_set<std::string> deps;
        if (type_name.is_capability || type_name.name == "Phys")
        {
            return deps;
        }
        if (structs_.contains(type_name.name) || enums_.contains(type_name.name))
        {
            deps.insert(std::string(type_name.name));
        }
        return deps;
    }

    // Emit struct/enum definitions in dependency order so that a by-value
    // field/payload referencing another nominal type is always complete at the
    // point of use. The forward typedefs only make tag names usable for
    // pointers; C requires complete types for by-value members.
    bool emit_types_in_dependency_order()
    {
        // Build the dependency graph: node -> set of nominal types it needs
        // defined before itself.
        std::vector<std::string> names;
        std::unordered_map<std::string, std::unordered_set<std::string>> deps_of;

        for (const auto& s : program_->structs)
        {
            names.push_back(std::string(s.name));
            auto& deps = deps_of[std::string(s.name)];
            for (const auto& f : s.fields)
            {
                const auto d = nominal_dependencies(f.type);
                deps.insert(d.begin(), d.end());
            }
        }
        for (const auto& e : program_->enums)
        {
            names.push_back(std::string(e.name));
            auto& deps = deps_of[std::string(e.name)];
            for (const auto& v : e.variants)
            {
                if (v.payload.has_value())
                {
                    const auto d = nominal_dependencies(*v.payload);
                    deps.insert(d.begin(), d.end());
                }
            }
        }

        // Deterministic topological order (stable within same-dep groups by
        // declaration order): repeatedly emit nodes whose deps are all emitted.
        std::unordered_set<std::string> emitted;
        std::vector<std::string> order;
        order.reserve(names.size());
        while (order.size() < names.size())
        {
            bool progress = false;
            for (const auto& n : names)
            {
                if (emitted.contains(n))
                {
                    continue;
                }
                bool ready = true;
                for (const auto& d : deps_of[n])
                {
                    if (!emitted.contains(d))
                    {
                        ready = false;
                        break;
                    }
                }
                if (ready)
                {
                    emitted.insert(n);
                    order.push_back(n);
                    progress = true;
                }
            }
            if (!progress)
            {
                // A dependency cycle (e.g. a self-referential struct) cannot be
                // emitted as by-value C; the front-end's type system should
                // reject recursive nominal types before codegen.
                for (const auto& n : names)
                {
                    if (!emitted.contains(n))
                    {
                        Span span{};
                        if (const auto it = structs_.find(n); it != structs_.end())
                        {
                            span = it->second->span;
                        }
                        else if (const auto it2 = enums_.find(n); it2 != enums_.end())
                        {
                            span = it2->second->span;
                        }
                        diags_.push_back(error_at(span, "cannot emit recursive nominal type '" +
                                                            n + "' in freestanding target"));
                        return false;
                    }
                }
            }
        }

        for (const auto& n : order)
        {
            if (const auto it = structs_.find(n); it != structs_.end())
            {
                emit_struct_decl(*it->second);
            }
            else if (const auto it2 = enums_.find(n); it2 != enums_.end())
            {
                emit_enum_decl(*it2->second);
            }
            if (!diags_.empty())
            {
                return false;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Functions
    // -----------------------------------------------------------------------

    void emit_function(const Function& fn, bool is_main)
    {
        // Contract-block ghost snapshots are verification-only: like `requires`
        // and `ensures`, they are enforced by the verifier before emission and
        // are not carried into the emitted C.
        //
        // Contracts are enforced by verification before emission; they are not
        // carried into the emitted C (the verifier is the gate).
        phys_vars_.clear();

        const std::string ret = return_c_type(fn);
        current_return_c_type_ = ret;
        current_function_name_ = std::string(fn.name);
        const std::string name = is_main ? "curlee_main" : mangle_name(fn.name);
        // Internal functions get `static` linkage; `main` becomes the exported
        // `curlee_main` entry point whose return type comes from return_c_type.
        writer_.line((is_main ? "" : "static ") + ret + " " + name + "(" +
                     params_c_list(fn) + ")");
        writer_.open_brace();

        // Reject unsupported parameter types (String/Vec/Set...).
        // Parameter types: Phys<T> lowers to `uintN_t*` (supported), capabilities
        // are dropped, Unit/String/Vec/Set are rejected. This differs from the
        // storage-position check (struct fields / enum payloads) where Phys and
        // Unit are also rejected.
        for (const auto& p : fn.params)
        {
            if (p.type.is_capability || p.type.name == "Phys")
            {
                continue;
            }
            if (!type_is_storable(p.type))
            {
                reject_unsupported_type(p.type, p.span,
                                        "parameter '" + std::string(p.name) + "'");
            }
        }
        if (!diags_.empty())
        {
            return;
        }

        // Track Phys<T> parameter variables (element kind; no literal address).
        for (const auto& p : fn.params)
        {
            if (p.type.name == "Phys" && p.type.type_arg.has_value())
            {
                phys_vars_.emplace(p.name,
                                   PhysVar{.element_kind = *p.type.type_arg, .addr_text = {}});
            }
        }

        emit_block_stmts(fn.body);
        if (!diags_.empty())
        {
            return;
        }

        // Emit a conservative fallback return only when the body does not
        // provably terminate in a return. The verifier proves reachability of
        // the explicit final return for -> Int functions, so a trailing
        // `return 0;` would be dead code and (for exhaustive match / while
        // loops) semantically wrong if ever reachable.
        if (ret != "void" && !body_terminates_in_return(fn.body))
        {
            writer_.line("return 0;");
        }

        writer_.close_brace();
        writer_.newline();
        current_return_c_type_.clear();
    }

    // True if control cannot reach the end of the block: the final statement
    // terminates in a return (or an if/else/match where every path returns).
    // Earlier statements are sequential and fall through to the last one, so
    // only the final statement matters for deciding whether a trailing fallback
    // return is dead code. This mirrors the reachability the verifier proves.
    bool body_terminates_in_return(const curlee::parser::Block& block)
    {
        if (block.stmts.empty())
        {
            return false;
        }
        return stmt_terminates_in_return(block.stmts.back());
    }

    bool stmt_terminates_in_return(const Stmt& stmt)
    {
        if (const auto* ret = std::get_if<ReturnStmt>(&stmt.node))
        {
            (void)ret;
            return true;
        }
        if (const auto* ifs = std::get_if<IfStmt>(&stmt.node))
        {
            if (ifs->else_block == nullptr)
            {
                return false;
            }
            return body_terminates_in_return(*ifs->then_block) &&
                   body_terminates_in_return(*ifs->else_block);
        }
        if (const auto* match = std::get_if<MatchStmt>(&stmt.node))
        {
            if (match->arms.empty())
            {
                return false;
            }
            for (const auto& arm : match->arms)
            {
                if (arm.body == nullptr || !body_terminates_in_return(*arm.body))
                {
                    return false;
                }
            }
            return true;
        }
        if (const auto* block = std::get_if<BlockStmt>(&stmt.node))
        {
            return body_terminates_in_return(*block->block);
        }
        if (const auto* uns = std::get_if<UnsafeStmt>(&stmt.node))
        {
            return body_terminates_in_return(*uns->body);
        }
        return false;
    }

    std::string return_c_type(const Function& fn)
    {
        if (!fn.return_type.has_value())
        {
            // The front-end rejects missing return annotations before codegen,
            // so this is defense-in-depth only.
            return "void";
        }
        return c_type_for_type_name(*fn.return_type);
    }

    // Capability parameters are dropped from the C parameter list (they carry
    // no runtime value on the freestanding target; the host gates authority at
    // the entry boundary). Phys<T> parameters become `uintN_t*` pointer
    // parameters; read()/write() on such a parameter dereference the pointer.
    std::string params_c_list(const Function& fn)
    {
        std::vector<std::string> parts;
        for (const auto& p : fn.params)
        {
            if (p.type.is_capability)
            {
                continue;
            }

            if (p.type.name == "Phys" && p.type.type_arg.has_value())
            {
                parts.push_back(std::string(c_type_for_phys_element(*p.type.type_arg)) + "* " +
                                std::string(p.name));
                continue;
            }

            parts.push_back(c_type_for_type_name(p.type) + " " + std::string(p.name));
        }

        if (parts.empty())
        {
            return "void";
        }

        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
            {
                out += ", ";
            }
            out += parts[i];
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Statements
    // -----------------------------------------------------------------------

    void emit_block_stmts(const curlee::parser::Block& block)
    {
        for (const auto& stmt : block.stmts)
        {
            emit_stmt(stmt);
            if (!diags_.empty())
            {
                return;
            }
        }
    }

    void emit_stmt(const Stmt& stmt)
    {
        std::visit([&](const auto& node) { emit_stmt_node(node, stmt.span); }, stmt.node);
    }

    void emit_stmt_node(const GhostLetStmt&, Span)
    {
        // Ghost snapshots are verification-only and produce no runtime code:
        // erase the statement from the C output (the verifier has already
        // discharged any obligations referencing it).
    }

    void emit_stmt_node(const LetStmt& stmt, Span span)
    {
        (void)span;

        // Fixed-size arrays (issue #278): `let q: [T; N] = [v; N];` emits
        // `T q[N] = { ... };`. The initializer must be an ArrayLiteralExpr
        // (the type checker enforces this); the repeat value is emitted once
        // and the brace list repeats it N times (a `{0}` zero-init shortcut is
        // used for the common `[0; N]` form).
        if (stmt.type.is_array())
        {
            const std::string elem_c = c_type_for_type_name(stmt.type);
            const std::string len = strip_underscores(*stmt.type.array_len);
            const auto* arr = std::get_if<ArrayLiteralExpr>(&stmt.value.node);
            if (arr == nullptr || arr->value == nullptr)
            {
                push_error(span, "array '" + std::string(stmt.name) +
                                     "': initializer must be an array repeat literal [v; N]");
                return;
            }
            // Zero-repeat shortcut: `int64_t q[8] = {0};` zero-fills all slots.
            const auto* lit = std::get_if<curlee::parser::IntExpr>(&arr->value->node);
            const bool zero_repeat =
                lit != nullptr && lit->lexeme.find_first_not_of("0_") == std::string_view::npos;
            std::string init;
            if (zero_repeat)
            {
                init = "{0}";
            }
            else
            {
                const std::string elem = emit_expr_as_text(*arr->value);
                if (!diags_.empty())
                {
                    return;
                }
                // The length was validated by the type checker (positive
                // integer literal); this is defense in depth against an
                // out-of-range or malformed lexeme.
                std::size_t count = 0;
                bool count_ok = false;
                try
                {
                    count = std::stoull(len);
                    count_ok = true;
                }
                catch (const std::exception&)
                {
                    count_ok = false;
                }
                if (!count_ok || count == 0 || count > 4096)
                {
                    push_error(span, "array '" + std::string(stmt.name) +
                                         "': invalid length '" + len + "'");
                    return;
                }
                std::string list = "{";
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (i != 0)
                    {
                        list += ", ";
                    }
                    list += elem;
                }
                list += "}";
                init = std::move(list);
            }
            writer_.line(elem_c + " " + std::string(stmt.name) + "[" + len + "] = " + init + ";");
            return;
        }

        const std::string c_type = c_type_for_type_name(stmt.type);

        // Record Phys<T> bindings so read()/write() on the name can lower to
        // the literal address deref.
        if (stmt.type.name == "Phys")
        {
            if (const auto* phys = std::get_if<PhysExpr>(&stmt.value.node); phys != nullptr)
            {
                phys_vars_.emplace(stmt.name,
                                   PhysVar{.element_kind = stmt.type.type_arg.value_or("U32"),
                                           .addr_text = strip_underscores(phys->lexeme)});
            }
        }

        const std::string init = emit_expr_as_text(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        if (c_type == "void")
        {
            // Phys<T> bindings are opaque pointer values with no scalar
            // storage; emit a comment (read()/write() lower to literal-address
            // derefs). Other void-typed lets (Unit/String/Vec/Set locals) are
            // rejected: they cannot be stored in freestanding C.
            if (stmt.type.name == "Phys")
            {
                writer_.line("/* unit binding " + std::string(stmt.name) + " */");
                return;
            }
            push_error(span, "local '" + std::string(stmt.name) + "': type '" +
                                 std::string(stmt.type.name) +
                                 "' is not supported in freestanding target");
            return;
        }

        writer_.line(c_type + " " + std::string(stmt.name) + " = " + init + ";");
    }

    void emit_stmt_node(const ReturnStmt& stmt, Span span)
    {
        if (!stmt.value.has_value())
        {
            writer_.line("return;");
            return;
        }

        // A value-returning statement is only valid when the function's C
        // return type is not void. The pre-pass (validate_signatures) rejects
        // Phys/Unit returns, and the front-end rejects `return <expr>;` in
        // Unit functions, so this is defense-in-depth against future changes.
        if (current_return_c_type_ == "void")
        {
            push_error(span, "cannot return a value from a function whose return "
                             "type lowers to void in freestanding C");
            return;
        }

        const std::string value = emit_expr_as_text(*stmt.value);
        if (!diags_.empty())
        {
            return;
        }
        writer_.line("return " + value + ";");
    }

    void emit_stmt_node(const ExprStmt& stmt, Span span)
    {
        (void)span;
        const std::string text = emit_expr_as_text(stmt.expr);
        if (!diags_.empty())
        {
            return;
        }
        writer_.line(text + ";");
    }

    void emit_stmt_node(const AssignStmt& stmt, Span span)
    {
        (void)span;
        // A Curlee assignment is a plain C assignment: the target is an
        // existing local binding (the front-end enforces this), so the C
        // variable of the same name already exists.
        const std::string value = emit_expr_as_text(stmt.value);
        if (!diags_.empty())
        {
            return;
        }
        writer_.line(std::string(stmt.name) + " = " + value + ";");
    }

    void emit_stmt_node(const IndexAssignStmt& stmt, Span span)
    {
        (void)span;
        // `arr[i] = v;` is a plain C array element write: the array binding
        // lowered to `T arr[N];` and the index/value are ordinary expressions.
        const std::string index = emit_expr_as_text(stmt.index);
        const std::string value = emit_expr_as_text(stmt.value);
        if (!diags_.empty())
        {
            return;
        }
        writer_.line(std::string(stmt.name) + "[" + index + "] = " + value + ";");
    }

    void emit_stmt_node(const BlockStmt& stmt, Span span)
    {
        (void)span;
        emit_block_stmts(*stmt.block);
    }

    void emit_stmt_node(const UnsafeStmt& stmt, Span span)
    {
        (void)span;
        emit_block_stmts(*stmt.body);
    }

    void emit_stmt_node(const IfStmt& stmt, Span span)
    {
        (void)span;
        const std::string cond = emit_expr_as_text(stmt.cond);
        if (!diags_.empty())
        {
            return;
        }

        writer_.line("if (" + cond + ")");
        writer_.open_brace();
        emit_block_stmts(*stmt.then_block);
        writer_.close_brace();

        if (stmt.else_block != nullptr)
        {
            writer_.line("else");
            writer_.open_brace();
            emit_block_stmts(*stmt.else_block);
            writer_.close_brace();
        }
    }

    void emit_stmt_node(const WhileStmt& stmt, Span span)
    {
        (void)span;
        const std::string cond = emit_expr_as_text(stmt.cond);
        if (!diags_.empty())
        {
            return;
        }

        writer_.line("while (" + cond + ")");
        writer_.open_brace();
        emit_block_stmts(*stmt.body);
        writer_.close_brace();
    }

    void emit_stmt_node(const MatchStmt& stmt, Span span)
    {
        (void)span;
        const std::string value = emit_expr_as_text(stmt.value);
        if (!diags_.empty())
        {
            return;
        }

        for (std::size_t i = 0; i < stmt.arms.size(); ++i)
        {
            const auto& arm = stmt.arms[i];
            const std::string cond = "(" + value + ").tag == " +
                                     mangle_variant_name(arm.pattern.enum_name,
                                                         arm.pattern.variant_name);
            writer_.line(i == 0 ? "if (" + cond + ")" : "else if (" + cond + ")");
            writer_.open_brace();

            if (arm.pattern.payload_name.has_value())
            {
                const curlee::parser::TypeName pt = payload_type(arm.pattern);
                const std::string c_type = c_type_for_type_name(pt);
                if (c_type != "void")
                {
                    writer_.line(c_type + " " + std::string(*arm.pattern.payload_name) +
                                 " = (" + value + ").payload." +
                                 std::string(arm.pattern.variant_name) + ";");
                }
            }

            if (arm.body != nullptr)
            {
                emit_block_stmts(*arm.body);
            }
            writer_.close_brace();
        }
    }

    curlee::parser::TypeName payload_type(const curlee::parser::MatchArmPattern& pattern)
    {
        if (const auto it = enums_.find(pattern.enum_name); it != enums_.end())
        {
            for (const auto& v : it->second->variants)
            {
                if (v.name == pattern.variant_name && v.payload.has_value())
                {
                    return *v.payload;
                }
            }
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Expressions
    // -----------------------------------------------------------------------

    std::string emit_expr_as_text(const Expr& expr)
    {
        return std::visit([&](const auto& node) { return emit_expr_node(node, expr.span); },
                          expr.node);
    }

    std::string emit_expr_node(const IntExpr& expr, Span span)
    {
        (void)span;
        // The lexeme may carry underscores only for PhysAddrLiteral-wrapped
        // write() values (e.g. 0xFF88_00) and phys() addresses; strip them for
        // valid C. Ordinary decimal Int literals never contain underscores.
        return strip_underscores(expr.lexeme);
    }

    std::string emit_expr_node(const BoolExpr& expr, Span span)
    {
        (void)span;
        return expr.value ? "1" : "0";
    }

    std::string emit_expr_node(const StringExpr&, Span span)
    {
        push_error(span, "builtin string literal is not supported in "
                         "freestanding target");
        return "0";
    }

    std::string emit_expr_node(const NameExpr& expr, Span span)
    {
        (void)span;
        return std::string(expr.name);
    }

    std::string emit_expr_node(const GroupExpr& expr, Span span)
    {
        (void)span;
        return "(" + emit_expr_as_text(*expr.inner) + ")";
    }

    std::string emit_expr_node(const UnaryExpr& expr, Span span)
    {
        using curlee::lexer::TokenKind;
        const std::string rhs = emit_expr_as_text(*expr.rhs);
        if (!diags_.empty())
        {
            return "0";
        }

        switch (expr.op)
        {
        case TokenKind::Bang:
            return "!(" + rhs + ")";
        case TokenKind::Minus:
            return "-(" + rhs + ")";
        case TokenKind::Tilde:
            return "~(" + rhs + ")";
        default:
            push_error(span, "unsupported unary operator in freestanding target");
            return "0";
        }
    }

    std::string emit_expr_node(const BinaryExpr& expr, Span span)
    {
        using curlee::lexer::TokenKind;

        const std::string lhs = emit_expr_as_text(*expr.lhs);
        if (!diags_.empty())
        {
            return "0";
        }
        const std::string rhs = emit_expr_as_text(*expr.rhs);
        if (!diags_.empty())
        {
            return "0";
        }

        switch (expr.op)
        {
        case TokenKind::Plus:
            return "(" + lhs + " + " + rhs + ")";
        case TokenKind::Minus:
            return "(" + lhs + " - " + rhs + ")";
        case TokenKind::Star:
            return "(" + lhs + " * " + rhs + ")";
        case TokenKind::Slash:
            return "(" + lhs + " / " + rhs + ")";
        case TokenKind::Percent:
            return "(" + lhs + " % " + rhs + ")";
        case TokenKind::Amp:
            return "(" + lhs + " & " + rhs + ")";
        case TokenKind::Pipe:
            return "(" + lhs + " | " + rhs + ")";
        case TokenKind::Caret:
            return "(" + lhs + " ^ " + rhs + ")";
        case TokenKind::ShiftLeft:
            return "(" + lhs + " << " + rhs + ")";
        case TokenKind::ShiftRight:
            return "(" + lhs + " >> " + rhs + ")";
        case TokenKind::EqualEqual:
            return "(" + lhs + " == " + rhs + ")";
        case TokenKind::BangEqual:
            return "(" + lhs + " != " + rhs + ")";
        case TokenKind::Less:
            return "(" + lhs + " < " + rhs + ")";
        case TokenKind::LessEqual:
            return "(" + lhs + " <= " + rhs + ")";
        case TokenKind::Greater:
            return "(" + lhs + " > " + rhs + ")";
        case TokenKind::GreaterEqual:
            return "(" + lhs + " >= " + rhs + ")";
        case TokenKind::AndAnd:
            return "(" + lhs + " && " + rhs + ")";
        case TokenKind::OrOr:
            return "(" + lhs + " || " + rhs + ")";
        default:
            push_error(span, "unsupported binary operator in freestanding target");
            return "0";
        }
    }

    std::string emit_expr_node(const MemberExpr& expr, Span span)
    {
        if (expr.base == nullptr)
        {
            push_error(span, "invalid member access in freestanding target");
            return "0";
        }
        const std::string base = emit_expr_as_text(*expr.base);
        if (!diags_.empty())
        {
            return "0";
        }
        return "(" + base + ")." + std::string(expr.member);
    }

    // Enum variant value: `Enum::Variant` -> `((curlee_Enum){ .tag =
    // curlee_Enum_Variant })`. When used in a tag comparison context the
    // callers (variant_is / match) compare `.tag` against the raw constant.
    std::string emit_expr_node(const ScopedNameExpr& expr, Span span)
    {
        (void)span;
        const std::string c_type = mangle_type_name(expr.lhs);
        const std::string tag = mangle_variant_name(expr.lhs, expr.rhs);

        // Only reference .payload for variants that actually carry one.
        bool has_payload = false;
        if (const auto it = enums_.find(expr.lhs); it != enums_.end())
        {
            for (const auto& v : it->second->variants)
            {
                if (v.name == expr.rhs && v.payload.has_value())
                {
                    has_payload = true;
                    break;
                }
            }
        }

        // The front-end requires a payload argument for payload-bearing
        // variants, so this path is defense-in-depth only. Guard against a
        // payload type with no storable C representation (its enum would have
        // been replaced by a placeholder with no `.payload` member). Initialize
        // the payload with `{0}`, valid for both scalars and aggregates.
        if (has_payload)
        {
            const curlee::parser::TypeName* pt = variant_payload_type(expr);
            if (pt == nullptr || !type_is_storable(*pt))
            {
                push_error(span, "enum variant '" + std::string(expr.lhs) + "::" +
                                 std::string(expr.rhs) +
                                 "' has a payload type with no storable C representation");
                return "0";
            }
        }

        std::string out = "((" + c_type + "){ .tag = " + tag;
        if (has_payload)
        {
            out += ", .payload." + std::string(expr.rhs) + " = {0}";
        }
        out += " })";
        return out;
    }

    // The bare tag constant for an enum variant (used in `.tag` comparisons).
    std::string emit_variant_tag_ref(const ScopedNameExpr& expr)
    {
        return mangle_variant_name(expr.lhs, expr.rhs);
    }

    std::string emit_expr_node(const StructLiteralExpr& expr, Span span)
    {
        const auto it = structs_.find(expr.type_name);
        if (it == structs_.end())
        {
            push_error(span, "unknown struct '" + std::string(expr.type_name) +
                             "' in freestanding target");
            return "0";
        }
        const auto* decl = it->second;

        // C99 compound literal: (curlee_Point){ .x = ..., .y = ... }
        std::string out = "((" + mangle_type_name(expr.type_name) + "){ ";
        bool first = true;
        for (const auto& field : decl->fields)
        {
            const auto* value = find_field_value(expr, field.name);
            if (value == nullptr)
            {
                push_error(span, "missing field '" + std::string(field.name) +
                                 "' in struct literal for '" + std::string(expr.type_name) +
                                 "'");
                return "0";
            }
            const std::string field_text = emit_expr_as_text(*value);
            if (!diags_.empty())
            {
                return "0";
            }
            if (!first)
            {
                out += ", ";
            }
            out += "." + std::string(field.name) + " = " + field_text;
            first = false;
        }
        out += " })";
        return out;
    }

    const curlee::parser::Expr* find_field_value(const StructLiteralExpr& literal,
                                                 std::string_view field_name)
    {
        for (const auto& f : literal.fields)
        {
            if (f.name == field_name)
            {
                return f.value.get();
            }
        }
        return nullptr;
    }

    std::string emit_enum_constructor(const ScopedNameExpr& scoped, const Expr* payload,
                                      Span span)
    {
        const auto it = enums_.find(scoped.lhs);
        if (it == enums_.end())
        {
            push_error(span, "unknown enum '" + std::string(scoped.lhs) +
                             "' in freestanding target");
            return "0";
        }

        const std::string c_type = mangle_type_name(scoped.lhs);
        const std::string tag = mangle_variant_name(scoped.lhs, scoped.rhs);

        // Defense-in-depth: the front-end requires a payload argument for
        // payload-bearing variants, and emit_enum_decl rejects non-storable
        // payload types up front. Guard here so a rejected payload can never
        // be referenced through `.payload.X`.
        if (payload != nullptr)
        {
            const curlee::parser::TypeName* pt = variant_payload_type(scoped);
            if (pt == nullptr || !type_is_storable(*pt))
            {
                push_error(span, "enum variant '" + std::string(scoped.lhs) + "::" +
                                 std::string(scoped.rhs) +
                                 "' has a payload type with no storable C representation");
                return "0";
            }
        }

        std::string out = "((" + c_type + "){ .tag = " + tag;
        if (payload != nullptr)
        {
            const std::string payload_text = emit_expr_as_text(*payload);
            if (!diags_.empty())
            {
                return "0";
            }
            out += ", .payload." + std::string(scoped.rhs) + " = " + payload_text;
        }
        out += " })";
        return out;
    }

    // The payload TypeName for a variant, or nullptr if it has none.
    const curlee::parser::TypeName* variant_payload_type(const ScopedNameExpr& variant) const
    {
        const auto it = enums_.find(variant.lhs);
        if (it == enums_.end())
        {
            return nullptr;
        }
        for (const auto& v : it->second->variants)
        {
            if (v.name == variant.rhs && v.payload.has_value())
            {
                return &*v.payload;
            }
        }
        return nullptr;
    }

    std::string emit_expr_node(const CallExpr& expr, Span span)
    {
        // Hosted builtins (print, __read_line, __fs_*, __tty_*, __rng_*,
        // vec/set ops) are rejected on the freestanding target. variant_is /
        // variant_unwrap are handled specially below. The builtin list is the
        // resolver's public is_builtin_call_name (single source of truth).
        if (const auto* callee_name = std::get_if<NameExpr>(&expr.callee->node);
            callee_name != nullptr)
        {
            if (callee_name->name == "variant_is" || callee_name->name == "variant_unwrap")
            {
                // Handled below.
            }
            else if (curlee::resolver::is_builtin_call_name(callee_name->name))
            {
                push_error(span, "builtin " + std::string(callee_name->name) +
                                     " is not supported in freestanding target");
                return "0";
            }
        }

        // variant_is(v, Enum::Variant) -> ((v).tag == curlee_Enum_Variant)
        if (const auto* callee_name = std::get_if<NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "variant_is")
        {
            if (expr.args.size() != 2)
            {
                push_error(span, "variant_is expects exactly 2 arguments");
                return "0";
            }
            const auto* variant = std::get_if<ScopedNameExpr>(&expr.args[1].node);
            if (variant == nullptr)
            {
                push_error(span, "variant_is expects second argument Enum::Variant");
                return "0";
            }
            const std::string value = emit_expr_as_text(expr.args[0]);
            if (!diags_.empty())
            {
                return "0";
            }
            return "((" + value + ").tag == " + emit_variant_tag_ref(*variant) + ")";
        }

        // variant_unwrap(v, Enum::Variant) -> ((v).payload.Variant)
        if (const auto* callee_name = std::get_if<NameExpr>(&expr.callee->node);
            callee_name != nullptr && callee_name->name == "variant_unwrap")
        {
            if (expr.args.size() != 2)
            {
                push_error(span, "variant_unwrap expects exactly 2 arguments");
                return "0";
            }
            const auto* variant = std::get_if<ScopedNameExpr>(&expr.args[1].node);
            if (variant == nullptr)
            {
                push_error(span, "variant_unwrap expects second argument Enum::Variant");
                return "0";
            }
            // Defensive: the type checker rejects unwrapping a payload-less
            // variant; guard here anyway against future front-end changes.
            if (!variant_has_payload(*variant))
            {
                push_error(span, "variant_unwrap requires a payload-bearing variant in "
                                 "freestanding target");
                return "0";
            }
            // Defense-in-depth: an enum whose payload type was rejected is
            // replaced by a placeholder with no `.payload` member; emitting
            // `.payload.X` against it would be invalid C.
            {
                const curlee::parser::TypeName* pt = variant_payload_type(*variant);
                if (pt == nullptr || !type_is_storable(*pt))
                {
                    push_error(span, "variant_unwrap on '" + std::string(variant->lhs) + "::" +
                                     std::string(variant->rhs) +
                                     "' has a payload type with no storable C representation");
                    return "0";
                }
            }
            const std::string value = emit_expr_as_text(expr.args[0]);
            if (!diags_.empty())
            {
                return "0";
            }
            return "((" + value + ").payload." + std::string(variant->rhs) + ")";
        }

        // Enum constructor: `Enum::Variant(payload)` or bare `Enum::Variant`.
        if (const auto* callee_scoped = std::get_if<ScopedNameExpr>(&expr.callee->node);
            callee_scoped != nullptr)
        {
            if (expr.args.size() > 1)
            {
                push_error(span, "enum constructors in freestanding target accept at most one "
                                 "payload argument");
                return "0";
            }
            const Expr* payload = expr.args.empty() ? nullptr : &expr.args[0];
            return emit_enum_constructor(*callee_scoped, payload, span);
        }

        // python_ffi.call is a hosted-only builtin (defense-in-depth; the
        // resolver already rejects python_ffi at resolve time).
        if (const auto* callee_member = std::get_if<MemberExpr>(&expr.callee->node);
            callee_member != nullptr && callee_member->base != nullptr)
        {
            const auto* base_name = std::get_if<NameExpr>(&callee_member->base->node);
            if (base_name != nullptr && base_name->name == "python_ffi" &&
                callee_member->member == "call")
            {
                push_error(span, "builtin python_ffi.call is not supported in "
                                 "freestanding target");
                return "0";
            }
        }

        // Plain internal function call.
        std::string_view callee_fn_name;
        if (const auto* callee_name = std::get_if<NameExpr>(&expr.callee->node);
            callee_name != nullptr)
        {
            callee_fn_name = callee_name->name;
        }
        else
        {
            push_error(span, "only name calls are supported in freestanding target");
            return "0";
        }

        if (!function_names_.contains(callee_fn_name))
        {
            push_error(span, "unknown function '" + std::string(callee_fn_name) +
                             "' in freestanding target");
            return "0";
        }

        // The call argument list must mirror params_c_list: capability args are
        // dropped, and Phys<T> args are passed as the pointer variable (a Phys
        // parameter) or the literal address (a phys<U>(addr) value).
        const Function* callee = find_function(callee_fn_name);
        if (callee == nullptr)
        {
            push_error(span, "unknown function '" + std::string(callee_fn_name) +
                             "' in freestanding target");
            return "0";
        }
        if (callee->is_ghost)
        {
            // Ghost functions are verification-only and erased from the C
            // output: a runnable function must never call one (it would dangle).
            push_error(span, "cannot call ghost function '" + std::string(callee_fn_name) +
                             "' from runnable code");
            return "0";
        }
        if (expr.args.size() != callee->params.size())
        {
            push_error(span, "call to '" + std::string(callee_fn_name) + "' expects " +
                             std::to_string(callee->params.size()) + " argument(s)");
            return "0";
        }

        // Extern callees resolve at link time to the verbatim symbol (no
        // curlee_ mangle); internal functions use the mangled name.
        std::string out = (callee->is_extern ? std::string(callee_fn_name)
                                             : mangle_name(callee_fn_name)) +
                          "(";
        bool first_arg = true;
        for (std::size_t i = 0; i < expr.args.size(); ++i)
        {
            const auto& param = callee->params[i];
            if (param.type.is_capability)
            {
                continue;
            }
            if (!first_arg)
            {
                out += ", ";
            }
            first_arg = false;

            if (param.type.name == "Phys")
            {
                // Phys arg: pass the pointer variable (name) or the literal
                // address cast (phys<U>(addr) value).
                out += emit_phys_arg(expr.args[i], span);
                if (!diags_.empty())
                {
                    return "0";
                }
                continue;
            }

            out += emit_expr_as_text(expr.args[i]);
            if (!diags_.empty())
            {
                return "0";
            }
        }
        out += ")";
        return out;
    }

    const Function* find_function(std::string_view name) const
    {
        for (const auto& f : program_->functions)
        {
            if (f.name == name)
            {
                return &f;
            }
        }
        return nullptr;
    }

    // Emit a Phys<T> argument to a function that takes a `uintN_t*` parameter:
    // a phys<U>(addr) literal passes the address cast; a named Phys binding or
    // parameter passes the name (the callee dereferences it).
    std::string emit_phys_arg(const Expr& arg, Span span)
    {
        if (const auto* phys = std::get_if<PhysExpr>(&arg.node); phys != nullptr)
        {
            // A literal phys<U>(addr) value: cast the embedded address.
            return "(void*)(uintptr_t)(" + strip_underscores(phys->lexeme) + ")";
        }
        if (const auto* name = std::get_if<NameExpr>(&arg.node); name != nullptr)
        {
            const auto it = phys_vars_.find(name->name);
            if (it == phys_vars_.end())
            {
                push_error(span, "unknown Phys value '" + std::string(name->name) +
                                 "' in freestanding target");
                return "0";
            }
            if (!it->second.addr_text.empty())
            {
                // A binding from phys<U>(addr): pass the embedded address cast.
                return "(void*)(uintptr_t)(" + it->second.addr_text + ")";
            }
            // A Phys parameter (or a binding from one): the parameter is
            // already a `uintN_t*`; pass it as-is so the callee can
            // dereference it.
            return std::string(name->name);
        }
        push_error(span, "unsupported Phys argument in freestanding target");
        return "0";
    }

    bool variant_has_payload(const ScopedNameExpr& variant)
    {
        const auto it = enums_.find(variant.lhs);
        if (it == enums_.end())
        {
            return false;
        }
        for (const auto& v : it->second->variants)
        {
            if (v.name == variant.rhs && v.payload.has_value())
            {
                return true;
            }
        }
        return false;
    }

    // `phys<U>(0xADDR)` records the variable and produces a C expression that
    // evaluates to the base address. read()/write() handle the deref.
    std::string emit_expr_node(const PhysExpr& expr, Span span)
    {
        (void)span;
        return "(void*)(uintptr_t)(" + strip_underscores(expr.lexeme) + ")";
    }

    // Resolve a Phys read/write base to (element kind, deref text).
    // A literal base (phys<U>(addr)) embeds the address; a named binding whose
    // addr_text is set (bound from phys<U>(addr)) also embeds it; a Phys
    // parameter (addr_text empty) dereferences the pointer parameter.
    // `verb` is used for diagnostics ("read" / "write").
    bool resolve_phys_base(const Expr& base, std::string_view verb, Span span,
                           std::string_view& element_kind, std::string& deref_text)
    {
        if (const auto* phys = std::get_if<PhysExpr>(&base.node); phys != nullptr)
        {
            element_kind = phys->element_kind;
            // Tail of `(*(volatile uintN_t*)(uintptr_t)(ADDR))`.
            deref_text = "(uintptr_t)(" + strip_underscores(phys->lexeme);
            return true;
        }
        if (const auto* name = std::get_if<NameExpr>(&base.node); name != nullptr)
        {
            const auto it = phys_vars_.find(name->name);
            if (it == phys_vars_.end())
            {
                push_error(span, std::string(verb) + "() on unknown Phys value '" +
                                 std::string(name->name) + "' in freestanding target");
                return false;
            }
            element_kind = it->second.element_kind;
            if (!it->second.addr_text.empty())
            {
                deref_text = "(uintptr_t)(" + it->second.addr_text;
            }
            else
            {
                // Phys parameter: dereference the pointer parameter.
                deref_text = "*(" + std::string(name->name) + ")";
            }
            return true;
        }
        push_error(span, std::string(verb) +
                             "() on a non-literal Phys value is not supported in freestanding "
                             "target");
        return false;
    }

    // True when the deref text dereferences a pointer parameter rather than a
    // literal address (i.e. it starts with `*(`).
    static bool is_param_deref_text(const std::string& deref_text)
    {
        return deref_text.rfind("*(", 0) == 0;
    }

    // Phys read: `(*(volatile uint32_t*)(0xADDR))` or `*param`.
    std::string emit_expr_node(const PhysReadExpr& expr, Span span)
    {
        if (expr.base == nullptr)
        {
            push_error(span, "missing Phys base in read()");
            return "0";
        }

        std::string_view element_kind;
        std::string deref_text;
        if (!resolve_phys_base(*expr.base, "read", span, element_kind, deref_text))
        {
            return "0";
        }

        const std::string c_type = std::string(c_type_for_phys_element(element_kind));
        if (is_param_deref_text(deref_text))
        {
            // Phys parameter: `*param` is already the typed lvalue.
            return "(" + deref_text + ")";
        }
        return "(*(volatile " + c_type + "*)" + deref_text + "))";
    }

    // Phys write: `(*(volatile uint32_t*)(0xADDR)) = (v);` or `*param = (v);`.
    std::string emit_expr_node(const PhysWriteExpr& expr, Span span)
    {
        if (expr.base == nullptr || expr.value == nullptr)
        {
            push_error(span, "missing Phys base or value in write()");
            return "0";
        }

        std::string_view element_kind;
        std::string deref_text;
        if (!resolve_phys_base(*expr.base, "write", span, element_kind, deref_text))
        {
            return "0";
        }

        const std::string value = emit_expr_as_text(*expr.value);
        if (!diags_.empty())
        {
            return "0";
        }

        const std::string c_type = std::string(c_type_for_phys_element(element_kind));
        if (is_param_deref_text(deref_text))
        {
            return "(" + deref_text + ") = (" + value + ")";
        }
        return "(*(volatile " + c_type + "*)" + deref_text + ")) = (" + value + ")";
    }

    // Indexed element read: `q[i]` lowers to `(q)[(i)]`. The array binding
    // was emitted as a plain C array by the let path (issue #278).
    std::string emit_expr_node(const IndexExpr& expr, Span span)
    {
        (void)span;
        if (expr.base == nullptr || expr.index == nullptr)
        {
            push_error(span, "invalid array index expression");
            return "0";
        }
        const std::string base = emit_expr_as_text(*expr.base);
        const std::string index = emit_expr_as_text(*expr.index);
        if (!diags_.empty())
        {
            return "0";
        }
        return "(" + base + ")[" + index + "]";
    }

    // Array repeat literal `[v; N]` in a value position. The freestanding
    // target only supports it as a `let` initializer (handled by the array-let
    // path in emit_stmt_node(const LetStmt&)); anywhere else the front-end has
    // rejected it, so this is defense in depth.
    std::string emit_expr_node(const ArrayLiteralExpr&, Span span)
    {
        push_error(span, "array literals are only supported as 'let' initializers in the "
                         "freestanding target");
        return "{0}";
    }

    // x86 port I/O builtins lower to calls of the prologue helpers with the
    // port argument embedded: `curlee_port_inb((uint16_t)(0x3FD))` for a
    // constant port, `curlee_port_inw((uint16_t)(io_base + 0x0C))` for a
    // runtime port (issue #276). The "Nd" asm constraint uses an 8-bit
    // immediate for constant ports in 0..255 and the DX register otherwise,
    // so runtime ports automatically emit the DX-register form — the helpers
    // need no change.
    std::string emit_expr_node(const PortIOExpr& expr, Span span)
    {
        // A constant literal port (single IntExpr token) emits the stripped
        // lexeme, byte-identical to the pre-#276 output; a runtime port emits
        // the general expression text.
        const std::string port = emit_expr_as_text(*expr.port);
        if (!diags_.empty())
        {
            return "0";
        }
        const std::string helper = "curlee_" + std::string(expr.op);

        if (expr.op.rfind("port_in", 0) == 0)
        {
            return helper + "((uint16_t)(" + port + "))";
        }

        if (expr.value == nullptr)
        {
            push_error(span, "missing port I/O write value");
            return "0";
        }
        const std::string value = emit_expr_as_text(*expr.value);
        if (!diags_.empty())
        {
            return "0";
        }
        return helper + "((uint16_t)(" + port + "), (" + value + "))";
    }

    // Runtime-address physical memory read (issue #279):
    // `phys_read_u8(addr)` -> `(*(volatile uint8_t*)(uintptr_t)(addr))`,
    // `phys_read_u16(addr)` -> `(*(volatile uint16_t*)(uintptr_t)(addr))`,
    // `phys_read_u32(addr)` -> `(*(volatile uint32_t*)(uintptr_t)(addr))`,
    // `phys_read_u64(addr)` -> `(*(volatile uint64_t*)(uintptr_t)(addr))`.
    // The address is a general Int/U64 expression (a runtime value such as
    // the boot-stub-captured multiboot2 info base plus a mutable byte cursor),
    // so the C text embeds the expression, mirroring how a runtime port emits
    // its expression into the helper call.
    std::string emit_expr_node(const RuntimePhysReadExpr& expr, Span span)
    {
        if (expr.addr == nullptr)
        {
            push_error(span, "missing runtime phys read address");
            return "0";
        }
        std::string_view element_kind;
        if (expr.op == "phys_read_u8")
        {
            element_kind = "U8";
        }
        else if (expr.op == "phys_read_u16")
        {
            element_kind = "U16";
        }
        else if (expr.op == "phys_read_u32")
        {
            element_kind = "U32";
        }
        else if (expr.op == "phys_read_u64")
        {
            element_kind = "U64";
        }
        else
        {
            push_error(span, "unknown runtime phys read builtin '" + std::string(expr.op) + "'");
            return "0";
        }

        const std::string addr = emit_expr_as_text(*expr.addr);
        if (!diags_.empty())
        {
            return "0";
        }
        const std::string c_type = std::string(c_type_for_phys_element(element_kind));
        return "(*(volatile " + c_type + "*)(uintptr_t)(" + addr + "))";
    }
};

} // namespace

CodegenResult codegen_freestanding_c(const curlee::parser::Program& program)
{
    CEmitter emitter;
    return emitter.run(program);
}

} // namespace curlee::codegen
