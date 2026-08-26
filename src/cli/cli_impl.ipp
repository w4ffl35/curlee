// SPDX-License-Identifier: MIT
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <curlee/bundle/bundle.h>
#include <curlee/cli/cli.h>
#include <curlee/codegen/codegen.h>
#include <curlee/compiler/emitter.h>
#include <curlee/diag/render.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/module_loader.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <curlee/types/type_check.h>
#include <curlee/verification/checker.h>
#include <curlee/vm/chunk_codec.h>
#include <curlee/vm/value.h>
#include <curlee/vm/vm.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace curlee::cli
{

namespace
{

#ifndef CURLEE_VERSION
#define CURLEE_VERSION "0.0.0"
#endif

#ifndef CURLEE_GIT_SHA
#define CURLEE_GIT_SHA "unknown"
#endif

#ifndef CURLEE_BUILD_TYPE
#define CURLEE_BUILD_TYPE "Unknown"
#endif

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

constexpr std::size_t kDefaultFuel = 10000;
constexpr std::string_view kDepsLockHeader = "CURLEE_DEPS_LOCK";
constexpr int kDepsLockFormatVersion = 1;

struct DependencyLockfile
{
    int format_version = kDepsLockFormatVersion;
    std::string entry_hash;
    std::vector<curlee::bundle::ImportPin> imports;
};

enum class DiagOutputFormat
{
    Text,
    Json,
};

enum class ProfileOutputFormat
{
    Text,
    Json,
};

struct RuntimeProfileOptions
{
    bool enabled = false;
    ProfileOutputFormat format = ProfileOutputFormat::Text;
};

curlee::runtime::Capabilities empty_caps()
{
    return {};
}

void print_usage(std::ostream& out)
{
    out << "curlee: verification-first language (early scaffold)\n\n";
    out << "usage:\n";
    out << "  curlee --help\n";
    out << "  curlee --version\n";
    out << "  curlee <file.curlee>\n";
    out << "  curlee lex <file.curlee>\n";
    out << "  curlee parse <file.curlee>\n";
    out << "  curlee check <file.curlee>\n";
    out << "  curlee run [--fuel <n>] [--seed <n>] [--profile] [--profile-format <text|json>] "
           "[--bundle <file.bundle>] [--cap <capability>]... <file.curlee>\n";
    out << "  curlee <lex|parse|check|run|build> [--diag-format <text|json>] ...\n";
    out << "  curlee build [--target freestanding-c] [--link] [-o out] <entry.curlee>\n";
    out << "    --target freestanding-c: emit freestanding C for the verified program\n";
    out << "      (the only supported target; no hosted builtins: no print, no String,\n";
    out << "      no Vec. The verification gate applies: no proof, no build).\n";
    out << "    --link: produce a bootable kernel ELF (kernel.elf) by compiling the\n";
    out << "            emitted C + runtime/crt0.S and linking with runtime/linker.ld\n";
    out << "            (requires a C compiler and ld; x86-64 multiboot2 image)\n";
    out << "    -o <path>: output path (default out.c; --link requires -o).\n";
    out << "  curlee run --cap phys.mem <file.curlee>  # phys.mem is freestanding-only\n";
    out << "    phys.mem: grants Phys<T> read()/write() MMIO access, the runtime-address\n";
    out << "    reads (phys_read_u8/u16/u32/u64, issue #279) and writes\n";
    out << "    (phys_write_u8/u16/u32/u64, issue #285), the address-of builtin\n";
    out << "    (addr_of(arr) for a Curlee-owned array's physical address, issue #286),\n";
    out << "    and the x86 port I/O builtins\n";
    out << "    (port_inb/port_outb/port_inw/port_outw/port_inl/port_outl). Port\n";
    out << "    programs are rejected by the VM (freestanding-only); use `curlee build`.\n";
    out << "  curlee fmt [--check] <file>\n";
    out << "  curlee bundle build [--root <dir>] [--stdlib-root <dir>] [--cap <capability>]... "
           "<entry.curlee> <out.bundle>\n";
    out << "  curlee bundle verify <file.bundle>\n";
    out << "  curlee bundle info <file.bundle>\n";
    out << "  curlee deps lock [--root <dir>] [--stdlib-root <dir>] <entry.curlee> <deps.lock>\n";
    out << "  curlee deps verify [--root <dir>] [--stdlib-root <dir>] <entry.curlee> <deps.lock>\n";
}

void print_build_usage(std::ostream& out)
{
    out << "curlee build: emit a verified program as freestanding C (or a bootable kernel "
           "ELF).\n\n";
    out << "usage:\n";
    out << "  curlee build [--target freestanding-c] [-o <path>] <entry.curlee>\n";
    out << "  curlee build [--target freestanding-c] --link -o <kernel.elf> <entry.curlee>\n\n";
    out << "The full check pipeline runs first (lex -> parse -> resolve -> type-check -> "
           "verify).\n";
    out << "The verification gate applies: no proof, no build (nothing is emitted on failure).\n\n";
    out << "flags:\n";
    out << "  --target <target>   codegen target; the only supported value is `freestanding-c`\n";
    out << "                      (default). Other targets are rejected.\n";
    out << "  --link              additionally compile the emitted C with\n";
    out << "                      `cc -ffreestanding -fno-builtin -nostdlib -c`, assemble\n";
    out << "                      runtime/crt0.S, and link with runtime/linker.ld into a\n";
    out << "                      bootable x86-64 multiboot2 kernel ELF. Requires -o.\n";
    out << "  -o <path>           output path (default: out.c). `-o -` writes the C to stdout.\n";
    out << "  --root <dir>        resolve the entry file relative to <dir>.\n";
    out << "  --stdlib-root <dir> additional stdlib search root (repeatable).\n\n";
    out << "freestanding subset (no hosted builtins):\n";
    out << "  - Supported: Int, Bool, U8/U16/U32/U64 arithmetic, structs/enums (storable\n";
    out << "    payloads only), match, if/else, while, verified contracts, extern fn,\n";
    out << "    module-level `static name: Type = expr;` state (issue #287), Phys<T> +\n";
    out << "    read()/write(), the runtime-address reads and writes\n";
    out << "    (phys_read_u8/u16/u32/u64 and phys_write_u8/u16/u32/u64; a general\n";
    out << "    Int/U64 address, issues #279/#285), the address-of builtin\n";
    out << "    (addr_of(arr): the physical address of a Curlee-owned fixed-size array\n";
    out << "    `let`/`static`, for DMA descriptor filling, issue #286) and the x86 port\n";
    out << "    I/O builtins (port_inb/port_outb/port_inw/port_outw/port_inl/port_outl;\n";
    out << "    constant or let-bound-base + constant-offset ports, issue #276) under\n";
    out << "    `unsafe` with `cap phys.mem`.\n";
    out << "  - Rejected: print, String, Vec, python_ffi, and all hosted builtins; Phys/Unit\n";
    out << "    cannot be stored (struct fields, enum payloads), returned, or used as\n";
    out << "    parameters.\n";
    out << "  - The VM never runs freestanding programs (`curlee run` rejects Phys/extern\n";
    out << "    bodies); `curlee build` is the execution path.\n";
}

// GCOVR_EXCL_START
std::string_view diag_severity_name(curlee::diag::Severity severity)
{
    switch (severity)
    {
    case curlee::diag::Severity::Error:
        return "error";
    case curlee::diag::Severity::Warning:
        return "warning";
    case curlee::diag::Severity::Note:
        return "note";
    }
    return "error";
}

std::string json_escape(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (const char ch : input)
    {
        switch (ch)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

void emit_span_json(std::ostream& out, const std::optional<curlee::source::Span>& span)
{
    if (!span.has_value())
    {
        out << "null";
        return;
    }
    out << "{\"start\":" << span->start << ",\"end\":" << span->end << "}";
}

void emit_diagnostic(std::ostream& out, const curlee::diag::Diagnostic& diagnostic,
                     const curlee::source::SourceFile& file, DiagOutputFormat format)
{
    if (format == DiagOutputFormat::Text)
    {
        out << curlee::diag::render(diagnostic, file);
        return;
    }

    out << "{\"kind\":\"diagnostic\",\"severity\":\"" << diag_severity_name(diagnostic.severity)
        << "\",\"message\":\"" << json_escape(diagnostic.message) << "\",\"file\":\""
        << json_escape(file.path) << "\",\"span\":";
    emit_span_json(out, diagnostic.span);
    out << ",\"notes\":[";
    for (std::size_t i = 0; i < diagnostic.notes.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << "{\"message\":\"" << json_escape(diagnostic.notes[i].message) << "\",\"span\":";
        emit_span_json(out, diagnostic.notes[i].span);
        out << "}";
    }
    out << "]}\n";
}

void emit_diagnostics(std::ostream& out, const std::vector<curlee::diag::Diagnostic>& diagnostics,
                      const curlee::source::SourceFile& file, DiagOutputFormat format)
{
    for (const auto& diagnostic : diagnostics)
    {
        emit_diagnostic(out, diagnostic, file, format);
    }
}

void emit_profile(std::ostream& out, const curlee::vm::VmProfile& profile, bool ok,
                  ProfileOutputFormat format)
{
    if (format == ProfileOutputFormat::Text)
    {
        out << "curlee profile: steps=" << profile.steps << " fuel_limit=" << profile.fuel_limit
            << " fuel_used=" << profile.fuel_used << " fuel_remaining=" << profile.fuel_remaining
            << " rng_seed="
            << (profile.rng_seed.has_value() ? std::to_string(*profile.rng_seed) : "none")
            << " ok=" << (ok ? "true" : "false") << "\n";
        return;
    }

    out << "{\"kind\":\"profile\",\"steps\":" << profile.steps
        << ",\"fuel_limit\":" << profile.fuel_limit << ",\"fuel_used\":" << profile.fuel_used
        << ",\"fuel_remaining\":" << profile.fuel_remaining << ",\"rng_seed\":";
    if (profile.rng_seed.has_value())
    {
        out << *profile.rng_seed;
    }
    else
    {
        out << "null";
    }
    out << ",\"ok\":" << (ok ? "true" : "false") << "}\n";
}

// Defense-in-depth drift oracle (issue #262): if the program declares a static
// `fuel` bound on `main` and the runtime profile shows `fuel_used` exceeding
// it, emit an advisory warning. The warning is NON-gating: the static proof is
// the primary gate, and the runtime profile only surfaces cost-model drift.
// Bounds that are not a simple integer literal (e.g. expressions over inputs)
// cannot be compared statically at runtime and are skipped.
void emit_fuel_drift_warning(const curlee::parser::Program& program,
                             const curlee::vm::VmProfile& profile)
{
    if (profile.fuel_used == 0)
    {
        return;
    }
    const curlee::parser::Function* main_fn = nullptr;
    for (const auto& f : program.functions)
    {
        if (f.name == "main")
        {
            main_fn = &f;
            break;
        }
    }
    if (main_fn == nullptr || !main_fn->fuel_bound.has_value())
    {
        return;
    }
    const auto& bound = *main_fn->fuel_bound;
    const auto* int_pred = std::get_if<curlee::parser::PredInt>(&bound.node);
    if (int_pred == nullptr)
    {
        return; // Non-literal bound: skip the advisory.
    }
    std::uint64_t declared = 0;
    const auto lexeme = std::string(int_pred->lexeme);
    const auto parsed = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), declared);
    if (parsed.ec != std::errc{} || parsed.ptr != lexeme.data() + lexeme.size())
    {
        return; // Unparsable literal: skip.
    }
    if (profile.fuel_used > declared)
    {
        std::cerr << "curlee warning: runtime fuel_used (" << profile.fuel_used
                  << ") exceeds the declared static fuel bound (" << declared
                  << ") on 'main'; the cost model may be too optimistic\n";
    }
}
// GCOVR_EXCL_STOP

bool ends_with(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool is_help_flag(std::string_view arg)
{
    return arg == "--help" || arg == "-h" || arg == "help";
}

bool is_version_flag(std::string_view arg)
{
    return arg == "--version" || arg == "version";
}

void print_version(std::ostream& out)
{
    out << "curlee " << CURLEE_VERSION;
    out << " sha=" << CURLEE_GIT_SHA;
    out << " build=" << CURLEE_BUILD_TYPE;
    out << "\n";
}

std::string join_csv(const std::vector<std::string>& xs)
{
    std::string out;
    for (std::size_t i = 0; i < xs.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(',');
        }
        out.append(xs[i]);
    }
    return out;
} // GCOVR_EXCL_LINE

std::string join_import_pins(const std::vector<curlee::bundle::ImportPin>& pins)
{
    std::string out;
    for (std::size_t i = 0; i < pins.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(',');
        }
        out.append(pins[i].path);
        out.push_back(':');
        out.append(pins[i].hash);
    }
    return out;
} // GCOVR_EXCL_LINE

// GCOVR_EXCL_START
std::vector<std::string> split_nonempty_csv(std::string_view value)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : value)
    {
        if (ch == ',')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }

    return parts;
}

bool import_pins_equal(const std::vector<curlee::bundle::ImportPin>& lhs,
                       const std::vector<curlee::bundle::ImportPin>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < lhs.size(); ++i)
    {
        if (lhs[i].path != rhs[i].path || lhs[i].hash != rhs[i].hash)
        {
            return false;
        }
    }

    return true;
}

std::string hash_text(std::string_view value)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size());
    for (const char c : value)
    {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return curlee::bundle::hash_bytes(bytes);
}

bool is_relative_inside(const std::filesystem::path& absolute_path,
                        const std::filesystem::path& absolute_root)
{
    const auto rel = absolute_path.lexically_relative(absolute_root);
    if (rel.empty())
    {
        return false;
    }

    const auto it = rel.begin();
    if (it != rel.end() && *it == "..")
    {
        return false;
    }

    return true;
}

std::optional<std::string>
stable_dependency_id_for_path(const std::filesystem::path& module_path,
                              const std::filesystem::path& entry_dir,
                              const std::vector<std::filesystem::path>& stdlib_roots)
{
    namespace fs = std::filesystem;

    const fs::path abs_module = fs::absolute(module_path).lexically_normal();
    const fs::path abs_entry = fs::absolute(entry_dir).lexically_normal();

    auto format_rel = [](std::string_view prefix, const fs::path& rel_path) -> std::string
    {
        std::string rel = rel_path.generic_string();
        if (ends_with(rel, ".curlee"))
        {
            rel.resize(rel.size() - std::string_view(".curlee").size());
        }
        return std::string(prefix) + rel;
    };

    if (is_relative_inside(abs_module, abs_entry))
    {
        return format_rel("entry/", abs_module.lexically_relative(abs_entry));
    }

    for (std::size_t i = 0; i < stdlib_roots.size(); ++i)
    {
        const fs::path abs_stdlib = fs::absolute(stdlib_roots[i]).lexically_normal();
        if (!is_relative_inside(abs_module, abs_stdlib))
        {
            continue;
        }

        return format_rel("stdlib" + std::to_string(i) + "/",
                          abs_module.lexically_relative(abs_stdlib));
    }

    return std::nullopt;
}

bool read_dependency_lockfile(const std::string& path, DependencyLockfile& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "failed to open lockfile";
        return false;
    }

    std::string line;
    if (!std::getline(in, line) || line != kDepsLockHeader)
    {
        error = "invalid lockfile header";
        return false;
    }

    bool saw_format = false;
    bool saw_entry_hash = false;
    std::vector<curlee::bundle::ImportPin> imports;
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "format_version")
        {
            int parsed = 0;
            const char* begin = value.data();
            const char* end = value.data() + value.size();
            const auto parse_result = std::from_chars(begin, end, parsed);
            if (parse_result.ec != std::errc{} || parse_result.ptr != end)
            {
                error = "invalid lockfile format version";
                return false;
            }
            out.format_version = parsed;
            saw_format = true;
            continue;
        }

        if (key == "entry_hash")
        {
            out.entry_hash = value;
            saw_entry_hash = true;
            continue;
        }

        if (key == "imports")
        {
            imports.clear();
            const auto entries = split_nonempty_csv(value);
            for (const auto& entry : entries)
            {
                const auto pos = entry.find(':');
                if (pos == std::string::npos || pos == 0 || pos + 1 >= entry.size())
                {
                    error = "invalid lockfile import pin";
                    return false;
                }

                imports.push_back(curlee::bundle::ImportPin{
                    .path = entry.substr(0, pos),
                    .hash = entry.substr(pos + 1),
                });
            }
        }
    }

    if (!saw_format)
    {
        error = "missing lockfile format version";
        return false;
    }

    if (out.format_version != kDepsLockFormatVersion)
    {
        error = "unsupported lockfile format version: " + std::to_string(out.format_version) +
                " (supported: " + std::to_string(kDepsLockFormatVersion) + ")";
        return false;
    }

    if (!saw_entry_hash || out.entry_hash.empty())
    {
        error = "missing entry_hash";
        return false;
    }

    out.imports = std::move(imports);
    return true;
}
// GCOVR_EXCL_STOP

bool write_dependency_lockfile(const std::string& path, const DependencyLockfile& lock,
                               std::string& error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "failed to open lockfile for writing";
        return false;
    }

    out << kDepsLockHeader << "\n";
    out << "format_version=" << kDepsLockFormatVersion << "\n";
    out << "entry_hash=" << lock.entry_hash << "\n";
    out << "imports=" << join_import_pins(lock.imports) << "\n";
    return true;
}

bool is_v1_forbidden_capability(std::string_view cap)
{
    // v1 capability surface decision (issue #257): only the Python-interop
    // capabilities are v1-forbidden (python_ffi is stubbed, so python.ffi and
    // python.sandbox are rejected at compile/check/bundle time).
    //
    // `phys.mem` is intentionally NOT v1-forbidden: it is a real freestanding
    // capability (Phys<T> read()/write() MMIO access, used by the
    // freestanding-c build target). It cannot be executed by the VM (Phys is
    // freestanding-only), but that is enforced by the front-end/emitter, not
    // by the capability gate. Declaring `cap phys.mem` in a bundle manifest is
    // therefore allowed (it is not a python interop capability).
    return cap == "python.ffi" || cap == "python.sandbox";
}

std::optional<std::string_view>
find_forbidden_v1_capability(const std::vector<std::string>& capabilities)
{
    for (const auto& cap : capabilities)
    {
        if (is_v1_forbidden_capability(cap))
        {
            return cap;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view>
find_forbidden_v1_capability(const curlee::runtime::Capabilities& capabilities)
{
    for (const auto& cap : capabilities)
    {
        if (is_v1_forbidden_capability(cap))
        {
            return cap;
        }
    }
    return std::nullopt;
}

bool chunk_uses_python_call(const curlee::vm::Chunk& chunk)
{
    std::size_t ip = 0;
    while (ip < chunk.code.size())
    {
        const auto op = static_cast<curlee::vm::OpCode>(chunk.code[ip++]);
        if (op == curlee::vm::OpCode::PythonCall)
        {
            return true;
        }

        std::size_t operand_bytes = 0;
        switch (op) // GCOVR_EXCL_LINE
        {
        case curlee::vm::OpCode::Constant:
        case curlee::vm::OpCode::LoadLocal:
        case curlee::vm::OpCode::StoreLocal:
        case curlee::vm::OpCode::Jump:
        case curlee::vm::OpCode::JumpIfFalse:
        case curlee::vm::OpCode::Call:
            operand_bytes = 2;
            break;

        case curlee::vm::OpCode::MakeEnum:
            operand_bytes = 5;
            break;

        case curlee::vm::OpCode::EnumIs:
        case curlee::vm::OpCode::EnumUnwrap:
            operand_bytes = 4;
            break;

        // GCOVR_EXCL_START
        case curlee::vm::OpCode::GetField:
            operand_bytes = 2;
            break;

        case curlee::vm::OpCode::MakeStruct:
        {
            if (ip + 3 >= chunk.code.size())
            {
                return false;
            }
            const std::uint16_t lo = chunk.code[ip + 2];
            const std::uint16_t hi = chunk.code[ip + 3];
            const std::uint16_t field_count = static_cast<std::uint16_t>(lo | (hi << 8));
            operand_bytes = static_cast<std::size_t>(4 + (2 * field_count));
            break;
        }
            // GCOVR_EXCL_STOP

        case curlee::vm::OpCode::Add:
        case curlee::vm::OpCode::Sub:
        case curlee::vm::OpCode::Mul:
        case curlee::vm::OpCode::Div:
        case curlee::vm::OpCode::Neg:
        case curlee::vm::OpCode::Not:
        case curlee::vm::OpCode::Equal:
        case curlee::vm::OpCode::NotEqual:
        case curlee::vm::OpCode::Less:
        case curlee::vm::OpCode::LessEqual:
        case curlee::vm::OpCode::Greater:
        case curlee::vm::OpCode::GreaterEqual:
        case curlee::vm::OpCode::Pop:
        case curlee::vm::OpCode::Return:
        case curlee::vm::OpCode::Ret:
        case curlee::vm::OpCode::Print:
        case curlee::vm::OpCode::ReadLine:
        case curlee::vm::OpCode::FsReadText:
        case curlee::vm::OpCode::FsWriteText:
        case curlee::vm::OpCode::TtyClear:
        case curlee::vm::OpCode::TtyWriteAt:
        case curlee::vm::OpCode::TtyFlush:
        case curlee::vm::OpCode::RngNextInt:
        case curlee::vm::OpCode::VecNew:
        case curlee::vm::OpCode::VecLen:
        case curlee::vm::OpCode::VecPush:
        case curlee::vm::OpCode::VecGet:
        case curlee::vm::OpCode::VecSet:
        case curlee::vm::OpCode::VecNewBool:
        case curlee::vm::OpCode::VecLenBool:
        case curlee::vm::OpCode::VecPushBool:
        case curlee::vm::OpCode::VecGetBool:
        case curlee::vm::OpCode::VecSetBool:
        case curlee::vm::OpCode::SetNewInt:
        case curlee::vm::OpCode::SetHasInt:
        case curlee::vm::OpCode::SetInsertInt:
        case curlee::vm::OpCode::PythonCall:
            operand_bytes = 0;
            break;

        default:   // GCOVR_EXCL_LINE
            break; // GCOVR_EXCL_LINE
        }

        if (ip + operand_bytes > chunk.code.size())
        {
            return false;
        }

        ip += operand_bytes;
    }

    return false;
}

int cmd_read_only(std::string_view cmd, const std::string& path,
                  const curlee::runtime::Capabilities& granted_caps, std::size_t fuel,
                  std::optional<std::uint64_t> rng_seed = std::nullopt,
                  bool use_window_graphics_backend = false,
                  DiagOutputFormat diag_format = DiagOutputFormat::Text,
                  RuntimeProfileOptions profile_options = {},
                  const std::vector<std::filesystem::path>& stdlib_roots = {},
                  const std::optional<std::filesystem::path>& entry_dir_override = std::nullopt,
                  std::vector<curlee::bundle::ImportPin>* out_import_pins = nullptr,
                  std::vector<std::uint8_t>* out_bytecode = nullptr,
                  std::string* out_c_source = nullptr)
{
    auto loaded = source::load_source_file(path);
    if (auto* err = std::get_if<source::LoadError>(&loaded))
    {
        const source::SourceFile pseudo_file{.path = path, .contents = ""}; // GCOVR_EXCL_LINE
        const diag::Diagnostic diag{
            .severity = diag::Severity::Error,
            .message = err->message,
            .span = std::nullopt,
            .notes = {},
            .file_path = std::nullopt,
            .function_name = {},
        };

        emit_diagnostic(std::cerr, diag, pseudo_file, diag_format);
        return kExitError;
    }

    const auto& file = std::get<source::SourceFile>(loaded);

    // Keep imported source files alive for the duration of this command.
    // The AST stores string_views into source buffers, so module contents must have
    // stable storage.
    std::vector<std::unique_ptr<source::SourceFile>> imported_files;
    std::unordered_map<std::string, std::size_t> imported_file_by_path;
    std::vector<parser::Program> imported_programs;
    std::unordered_map<std::string, std::size_t> imported_by_path;
    std::unordered_map<std::string, std::string> dependency_id_by_path;

    std::optional<types::TypeInfo> last_type_info;

    // Non-fatal Note diagnostics produced by verification (e.g. the "extern
    // boundary: contract assumed, not verified" notice). These are rendered but
    // never cause the command to fail.
    std::vector<diag::Diagnostic> verify_notes;

    // Function name -> owning source file path, populated during the import
    // merge. Used to render codegen diagnostics (whose spans are plain byte
    // offsets without file identity) against the correct file.
    std::unordered_map<std::string, std::string> function_file_map_;

    auto run_checks = [&](parser::Program& program) -> bool
    {
        namespace fs = std::filesystem;

        last_type_info.reset();

        constexpr int kMaxImportDepth = 64;

        auto normalize_path = [](const std::string& p) -> std::string
        { return fs::absolute(fs::path(p)).lexically_normal().string(); };

        const fs::path entry_dir = entry_dir_override.value_or(fs::path(file.path).parent_path());

        imported_files.clear();
        imported_file_by_path.clear();
        imported_programs.clear();
        imported_by_path.clear();
        dependency_id_by_path.clear();

        std::unordered_set<std::string> visiting;
        std::unordered_set<std::string> visited;
        std::vector<std::string> merge_order;

        auto render_diags = [&](const std::vector<diag::Diagnostic>& ds,
                                const source::SourceFile& f) -> void
        { emit_diagnostics(std::cerr, ds, f, diag_format); };

        struct ImportLoadResult
        {
            source::SourceFile file;
            fs::path path;
            std::string key;
        };

        auto load_import =
            [&](const source::SourceFile& importing_file,
                const parser::ImportDecl& imp) -> std::variant<ImportLoadResult, diag::Diagnostic>
        {
            resolver::ModuleSearchOptions opts;
            opts.importing_file_dir = fs::path(importing_file.path).parent_path();
            opts.entry_dir = entry_dir;
            opts.stdlib_roots = stdlib_roots;
            opts.debug_trace = std::getenv("CURLEE_DEBUG_IMPORTS") != nullptr;

            const auto found = resolver::resolve_module_path(imp.path, opts);

            if (!found)
            {
                diag::Diagnostic d = found.error();
                d.span = imp.span;
                return d;
            }

            const auto& mod_path = found->path;
            const auto loaded = source::load_source_file(mod_path.string());
            if (auto* err = std::get_if<source::LoadError>(&loaded))
            {
                diag::Diagnostic d;
                d.severity = diag::Severity::Error;
                d.message = "failed to load imported module: " + err->message;
                d.span = imp.span;
                return d;
            }

            auto dep_file = std::get<source::SourceFile>(loaded);

            ImportLoadResult ok;
            ok.file = std::move(dep_file);
            ok.path = mod_path;
            ok.key = found->canonical_path;
            return ok;
        };

        auto check_module = [&](const source::SourceFile& mod_file, int depth,
                                auto&& check_module_ref) -> bool
        {
            if (depth > kMaxImportDepth)
            {
                const diag::Diagnostic d{
                    .severity = diag::Severity::Error,
                    .message = "import graph too deep (possible cycle)",
                    .span = std::nullopt,
                    .notes = {},
                    .file_path = std::nullopt,
                    .function_name = {},
                };
                emit_diagnostic(std::cerr, d, mod_file, diag_format);
                return false;
            }

            const std::string key = normalize_path(mod_file.path);

            // Ensure the module's source buffer outlives any AST nodes we store.
            std::size_t file_idx = 0;
            if (const auto it = imported_file_by_path.find(key); it != imported_file_by_path.end())
            {
                file_idx = it->second;
            }
            else
            {
                imported_files.push_back(std::make_unique<source::SourceFile>(mod_file));
                file_idx = imported_files.size() - 1;
                imported_file_by_path.emplace(key, file_idx);
            }
            const source::SourceFile& stable_file = *imported_files[file_idx];

            if (!dependency_id_by_path.contains(key))
            {
                const auto dependency_id =
                    stable_dependency_id_for_path(stable_file.path, entry_dir, stdlib_roots);
                if (!dependency_id.has_value())
                {
                    const diag::Diagnostic d{
                        // GCOVR_EXCL_LINE
                        .severity = diag::Severity::Error,
                        .message =
                            "cannot generate deterministic dependency pin for import outside "
                            "entry/stdlib roots",
                        .span = std::nullopt,
                        .notes =
                            {
                                diag::Related{.message = "module: " + stable_file.path,
                                              .span = std::nullopt},
                                diag::Related{.message =
                                                  "set --root and --stdlib-root so imports resolve "
                                                  "inside deterministic roots",
                                              .span = std::nullopt},
                            },
                        .file_path = std::nullopt, // GCOVR_EXCL_LINE
                        .function_name = {},       // GCOVR_EXCL_LINE
                    };
                    emit_diagnostic(std::cerr, d, stable_file, diag_format);
                    return false;
                }

                dependency_id_by_path.emplace(key, *dependency_id);
            }

            if (visited.contains(key))
            {
                return true;
            }

            visiting.insert(key);

            const auto lexed = lexer::lex(stable_file.contents);
            if (std::holds_alternative<diag::Diagnostic>(lexed))
            {
                emit_diagnostic(std::cerr, std::get<diag::Diagnostic>(lexed), stable_file,
                                diag_format);
                visiting.erase(key);
                return false;
            }

            const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
            auto parsed = parser::parse(toks);
            if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
            {
                render_diags(std::get<std::vector<diag::Diagnostic>>(parsed), stable_file);
                visiting.erase(key);
                return false;
            }

            auto mod_program = std::move(std::get<parser::Program>(parsed));

            for (const auto& f : mod_program.functions)
            {
                if (f.name == "main")
                {
                    const diag::Diagnostic d{
                        .severity = diag::Severity::Error,
                        .message = "imported modules must not define 'main'",
                        .span = f.span,
                        .notes = {},
                        .file_path = std::nullopt,
                        .function_name = {},
                    };
                    emit_diagnostic(std::cerr, d, stable_file, diag_format);
                    visiting.erase(key);
                    return false;
                } // GCOVR_EXCL_LINE
            }

            for (const auto& imp : mod_program.imports)
            {
                const auto dep_loaded = load_import(stable_file, imp);
                if (auto* d = std::get_if<diag::Diagnostic>(&dep_loaded))
                {
                    emit_diagnostic(std::cerr, *d, stable_file, diag_format);
                    visiting.erase(key);
                    return false;
                } // GCOVR_EXCL_LINE

                const auto& dep_ok = std::get<ImportLoadResult>(dep_loaded);
                if (visiting.contains(dep_ok.key))
                {
                    diag::Diagnostic d;
                    d.severity = diag::Severity::Error;
                    d.message = "import cycle detected";
                    d.span = imp.span;
                    const diag::Related note{.message = "cycle involves " + dep_ok.path.string(),
                                             .span = std::nullopt};
                    d.notes.push_back(note); // GCOVR_EXCL_LINE
                    emit_diagnostic(std::cerr, d, stable_file, diag_format);
                    visiting.erase(key);
                    return false;
                }

                if (!check_module_ref(dep_ok.file, depth + 1, check_module_ref))
                {
                    visiting.erase(key);
                    return false;
                }
            }

            const auto resolved =
                resolver::resolve(mod_program, stable_file, std::optional{entry_dir}, stdlib_roots);
            if (std::holds_alternative<std::vector<diag::Diagnostic>>(resolved))
            {
                render_diags(std::get<std::vector<diag::Diagnostic>>(resolved), stable_file);
                visiting.erase(key);
                return false;
            }

            const auto typed = types::type_check(mod_program);
            if (std::holds_alternative<std::vector<diag::Diagnostic>>(typed))
            {
                render_diags(std::get<std::vector<diag::Diagnostic>>(typed), stable_file);
                visiting.erase(key);
                return false;
            }

            // Store the module program so we can merge functions into the main program for
            // downstream type checking / verification / emission.
            imported_programs.push_back(std::move(mod_program));
            imported_by_path.emplace(key, imported_programs.size() - 1);
            merge_order.push_back(key);

            visiting.erase(key);
            visited.insert(key);
            return true;
        }; // GCOVR_EXCL_LINE

        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            emit_diagnostic(std::cerr, std::get<diag::Diagnostic>(lexed), file, diag_format);
            return false;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            render_diags(std::get<std::vector<diag::Diagnostic>>(parsed), file);
            return false;
        }

        program = std::move(std::get<parser::Program>(parsed));

        // Verify imported modules (and their imports) first.
        visiting.insert(normalize_path(file.path));
        for (const auto& imp : program.imports)
        {
            const auto dep_loaded = load_import(file, imp);
            if (auto* d = std::get_if<diag::Diagnostic>(&dep_loaded))
            {
                emit_diagnostic(std::cerr, *d, file, diag_format);
                return false;
            }

            const auto& dep_ok = std::get<ImportLoadResult>(dep_loaded);
            if (visiting.contains(dep_ok.key))
            {
                diag::Diagnostic d;
                d.severity = diag::Severity::Error;
                d.message = "import cycle detected";
                d.span = imp.span;
                const diag::Related note{.message = "cycle involves " + dep_ok.path.string(),
                                         .span = std::nullopt};
                d.notes.push_back(note); // GCOVR_EXCL_LINE
                emit_diagnostic(std::cerr, d, file, diag_format);
                return false;
            }

            if (!check_module(dep_ok.file, 1, check_module))
            {
                return false;
            }
        }
        visiting.erase(normalize_path(file.path));

        // Merge imported module functions into the main program so callers can reference them.
        // Imports have already been checked/verified above, so we expect no new errors.
        {
            // Record which source file each function came from, so codegen
            // diagnostics (whose spans are plain byte offsets) can be rendered
            // against the owning file. Entry-file functions map to the entry
            // path.
            function_file_map_.clear();
            for (const auto& f : program.functions)
            {
                function_file_map_.emplace(std::string(f.name), file.path);
            }

            std::unordered_set<std::string> seen;
            for (const auto& f : program.functions)
            {
                seen.insert(std::string(f.name));
            }

            // Deterministic merge order: lexicographic by normalized file path.
            std::vector<std::string> keys;
            keys.reserve(imported_by_path.size());
            for (const auto& [k, _] : imported_by_path)
            {
                keys.push_back(k);
            }
            std::sort(keys.begin(), keys.end());

            for (const auto& key : keys)
            {
                const std::size_t idx = imported_by_path.at(key);
                auto& mod_program = imported_programs[idx];
                for (auto& f : mod_program.functions)
                {
                    const std::string name(f.name);
                    if (seen.contains(name))
                    {
                        diag::Diagnostic d;
                        d.severity = diag::Severity::Error;
                        d.message = "duplicate function across modules: '" + name + "'";
                        d.span = std::nullopt;
                        const diag::Related note{.message = "conflict while importing " + key,
                                                 .span = std::nullopt};
                        d.notes.push_back(note); // GCOVR_EXCL_LINE
                        emit_diagnostic(std::cerr, d, file, diag_format);
                        return false;
                    }
                    seen.insert(name);
                    function_file_map_.emplace(name, imported_files[idx]->path);
                    program.functions.push_back(std::move(f));
                }
            }

            // Expression IDs are per-parse; after merging we must make them unique.
            parser::reassign_expr_ids(program);
        }

        const auto resolved = resolver::resolve(program, file, entry_dir, stdlib_roots);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(resolved))
        {
            render_diags(std::get<std::vector<diag::Diagnostic>>(resolved), file);
            return false;
        }

        const auto typed = types::type_check(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(typed))
        {
            render_diags(std::get<std::vector<diag::Diagnostic>>(typed), file);
            return false;
        }

        const auto& type_info = std::get<types::TypeInfo>(typed);
        last_type_info = type_info;

        // Collect non-fatal Notes (extern boundaries) so they render without
        // failing the command; `verify` still returns Verified{} when the only
        // diagnostics are Notes.
        verify_notes.clear();
        const auto verified = verification::verify(program, type_info, &verify_notes);

        if (std::holds_alternative<std::vector<diag::Diagnostic>>(verified))
        {
            std::vector<diag::Diagnostic> filtered;
            for (const auto& d : std::get<std::vector<diag::Diagnostic>>(verified))
            {
                if (d.message.rfind("verification does not support type '", 0) == 0)
                {
                    continue;
                }
                // Defense in depth: Notes never fail a command, regardless of
                // producer.
                if (d.severity == diag::Severity::Note)
                {
                    verify_notes.push_back(d);
                    continue;
                }
                filtered.push_back(d);
            }
            if (!filtered.empty())
            {
                render_diags(filtered, file);
                return false;
            }
        }

        // Render non-fatal Notes to stderr (like diagnostics) but keep the
        // command successful.
        if (!verify_notes.empty())
        {
            emit_diagnostics(std::cerr, verify_notes, file, diag_format);
        }

        return true;
    };

    if (cmd == "lex")
    {
        const auto res = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(res))
        {
            emit_diagnostic(std::cerr, std::get<diag::Diagnostic>(res), file, diag_format);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(res);
        std::cout << "curlee lex: " << toks.size() << " tokens\n";
        return kExitOk;
    }

    if (cmd == "parse")
    {
        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            emit_diagnostic(std::cerr, std::get<diag::Diagnostic>(lexed), file, diag_format);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            emit_diagnostics(std::cerr, std::get<std::vector<diag::Diagnostic>>(parsed), file,
                             diag_format);
            return kExitError;
        }

        const auto& program = std::get<parser::Program>(parsed);
        std::cout << parser::dump(program) << "\n";
        return kExitOk;
    }

    if (cmd == "check")
    {
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        return kExitOk;
    }

    if (cmd == "build")
    {
        // `curlee build` runs the full lex -> parse -> resolve -> type-check ->
        // verify pipeline and refuses to emit anything unless verification
        // succeeds (no proof, no build).
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        const auto emitted = curlee::codegen::codegen_freestanding_c(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(emitted))
        {
            // Codegen diagnostics carry the name of the function whose body
            // produced them (spans are per-file byte offsets that collide
            // across imported files). Resolve the owning file via the
            // function->file map recorded during the import merge; fall back to
            // the entry file.
            for (auto& d : std::get<std::vector<diag::Diagnostic>>(emitted))
            {
                const source::SourceFile* diag_file = &file;
                if (!d.function_name.empty())
                {
                    const auto it = function_file_map_.find(d.function_name);
                    if (it != function_file_map_.end())
                    {
                        if (it->second == file.path)
                        {
                            diag_file = &file;
                        }
                        else
                        {
                            for (const auto& imported : imported_files)
                            {
                                if (imported->path == it->second)
                                {
                                    diag_file = imported.get();
                                    break;
                                }
                            }
                        }
                    }
                }
                emit_diagnostic(std::cerr, d, *diag_file, diag_format);
            }
            return kExitError;
        }

        if (out_c_source == nullptr)
        {
            // GCOVR_EXCL_LINE
            return kExitError; // GCOVR_EXCL_LINE
        }
        *out_c_source = std::get<std::string>(emitted);
        return kExitOk;
    }

    if (cmd == "run")
    {
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        if (granted_caps.contains("rng.seeded") && !rng_seed.has_value()) // GCOVR_EXCL_LINE
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = "missing RNG seed; pass --seed <n>";
            d.span = std::nullopt;
            emit_diagnostic(std::cerr, d, file, diag_format);
            return kExitError;
        }

        for (const auto& req : last_type_info.value().required_capabilities)
        {
            const std::string req_name(req.name);
            if (!granted_caps.contains(req_name))
            {
                diag::Diagnostic d;
                d.severity = diag::Severity::Error;
                d.message = "capability not granted: " + req_name;
                d.span = req.span;
                diag::Related note;
                note.message = "grant it with: curlee run --cap " + req_name + " <file.curlee>";
                note.span = std::nullopt;
                d.notes.push_back(note);
                emit_diagnostic(std::cerr, d, file, diag_format);
                return kExitError;
            }
        }

        const auto emitted = compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(emitted))
        {
            emit_diagnostics(std::cerr, std::get<std::vector<diag::Diagnostic>>(emitted), file,
                             diag_format);
            return kExitError;
        }

        const auto& chunk = std::get<vm::Chunk>(emitted);
        vm::VM machine;
        vm::VmRunOptions run_options;
        run_options.use_window_graphics_backend = use_window_graphics_backend;
        const auto result = machine.run(chunk, fuel, granted_caps, rng_seed, run_options);
        if (!result.ok)
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = result.error;
            d.span = result.error_span;
            emit_diagnostic(std::cerr, d, file, diag_format);
            if (profile_options.enabled)
            {
                emit_profile(std::cerr, result.profile, false, profile_options.format);
                emit_fuel_drift_warning(program, result.profile);
            }
            return kExitError;
        }

        std::cout << "curlee run: result " << vm::to_string(result.value) << "\n";
        if (profile_options.enabled)
        {
            emit_profile(std::cout, result.profile, true, profile_options.format);
            emit_fuel_drift_warning(program, result.profile);
        }
        return kExitOk;
    }

    if (cmd == "bundle-build")
    {
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        const auto emitted = compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(emitted))
        {
            emit_diagnostics(std::cerr, std::get<std::vector<diag::Diagnostic>>(emitted), file,
                             diag_format);
            return kExitError;
        }

        if (out_import_pins == nullptr || out_bytecode == nullptr) // GCOVR_EXCL_LINE
        {
            return kExitError; // GCOVR_EXCL_LINE
        }

        std::vector<std::string> keys;
        keys.reserve(dependency_id_by_path.size());
        for (const auto& [k, _] : dependency_id_by_path)
        {
            keys.push_back(k);
        }
        std::sort(keys.begin(), keys.end(),
                  [&](const std::string& lhs, const std::string& rhs) -> bool
                  {
                      const auto& lhs_id = dependency_id_by_path.at(lhs);
                      const auto& rhs_id = dependency_id_by_path.at(rhs);
                      if (lhs_id == rhs_id) // GCOVR_EXCL_LINE
                      {
                          return lhs < rhs; // GCOVR_EXCL_LINE
                      }
                      return lhs_id < rhs_id;
                  });

        out_import_pins->clear();
        out_import_pins->reserve(keys.size());

        for (const auto& key : keys)
        {
            const std::size_t idx = imported_file_by_path.at(key);
            const auto& src = imported_files[idx]->contents;
            std::vector<std::uint8_t> bytes;
            bytes.reserve(src.size());
            for (const char ch : src)
            {
                bytes.push_back(static_cast<std::uint8_t>(ch));
            }

            curlee::bundle::ImportPin pin;
            pin.path = dependency_id_by_path.at(key);
            pin.hash = curlee::bundle::hash_bytes(bytes);
            out_import_pins->push_back(std::move(pin));
        }

        const auto& chunk = std::get<vm::Chunk>(emitted);
        *out_bytecode = curlee::vm::encode_chunk(chunk);

        return kExitOk;
    }

    std::cerr << "error: unknown command: " << cmd << "\n";
    return kExitUsage;
}

int cmd_run_bundle(const curlee::bundle::Bundle& bundle, const std::string& entry_path,
                   const curlee::runtime::Capabilities& granted_caps, std::size_t fuel,
                   std::optional<std::uint64_t> rng_seed, bool use_window_graphics_backend,
                   DiagOutputFormat diag_format, RuntimeProfileOptions profile_options)
{
    auto loaded = source::load_source_file(entry_path);
    if (auto* err = std::get_if<source::LoadError>(&loaded))
    {
        source::SourceFile pseudo_file{};
        pseudo_file.path = entry_path;
        pseudo_file.contents = "";
        const diag::Diagnostic diag{
            .severity = diag::Severity::Error,
            .message = err->message,
            .span = std::nullopt,
            .notes = {},
            .file_path = std::nullopt,
            .function_name = {},
        };

        emit_diagnostic(std::cerr, diag, pseudo_file, diag_format);
        return kExitError;
    }

    const auto& file = std::get<source::SourceFile>(loaded);

    if (const auto forbidden = find_forbidden_v1_capability(bundle.manifest.capabilities);
        forbidden.has_value())
    {
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = "bundle capability not part of Curlee v1 surface: " + std::string(*forbidden);
        d.span = curlee::source::Span{.start = 0, .end = 0};
        emit_diagnostic(std::cerr, d, file, diag_format);
        return kExitError;
    }

    if (const auto forbidden = find_forbidden_v1_capability(granted_caps); forbidden.has_value())
    {
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = "capability not part of Curlee v1 surface: " + std::string(*forbidden);
        d.span = curlee::source::Span{.start = 0, .end = 0};
        emit_diagnostic(std::cerr, d, file, diag_format);
        return kExitError;
    }

    curlee::runtime::Capabilities effective_caps;
    // GCOVR_EXCL_START
    if (use_window_graphics_backend)
    {
        if (!granted_caps.contains("gfx.window"))
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = "capability not granted: gfx.window";
            d.span = std::nullopt;
            const diag::Related note{
                .message =
                    "grant it with: curlee run --graphics=window --cap gfx.window <file.curlee>",
                .span = std::nullopt,
            };
            d.notes.push_back(note);
            emit_diagnostic(std::cerr, d, file, diag_format);
            return kExitError;
        }
        effective_caps.insert("gfx.window");
    }
    // GCOVR_EXCL_STOP

    for (const auto& cap : bundle.manifest.capabilities)
    {
        if (!granted_caps.contains(cap))
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = "missing capability required by bundle: " + cap;
            d.span = curlee::source::Span{.start = 0, .end = 0};
            const diag::Related required_note{
                .message = "bundle manifest requires capability '" + cap + "'",
                .span = std::nullopt,
            };
            d.notes.push_back(required_note);

            const diag::Related grant_note{
                .message = "grant it with: curlee run --cap " + cap +
                           " --bundle <file.bundle> <file.curlee>",
                .span = std::nullopt,
            };
            d.notes.push_back(grant_note);
            emit_diagnostic(std::cerr, d, file, diag_format);
            return kExitError; // GCOVR_EXCL_LINE
        }
        effective_caps.insert(cap);
    }

    const auto decoded = curlee::vm::decode_chunk(bundle.bytecode);
    if (const auto* decode_err = std::get_if<curlee::vm::ChunkDecodeError>(&decoded))
    {
        const diag::Diagnostic d{
            .severity = diag::Severity::Error,
            .message = "invalid bundle bytecode: " + decode_err->message,
            .span = std::nullopt,
            .notes = {},
            .file_path = std::nullopt,
            .function_name = {},
        };
        emit_diagnostic(std::cerr, d, file, diag_format);
        return kExitError;
    }

    const auto& chunk = std::get<curlee::vm::Chunk>(decoded);
    if (chunk_uses_python_call(chunk))
    {
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = "bundle bytecode uses python interop opcode not supported in Curlee v1";
        d.span = curlee::source::Span{.start = 0, .end = 0};
        emit_diagnostic(std::cerr, d, file, diag_format);
        return kExitError;
    }

    vm::VM machine;

    vm::VmRunOptions run_options;
    run_options.use_window_graphics_backend = use_window_graphics_backend;
    const auto result = machine.run(chunk, fuel, effective_caps, rng_seed, run_options);
    if (!result.ok)
    {
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = result.error;
        d.span = result.error_span;
        emit_diagnostic(std::cerr, d, file, diag_format);
        if (profile_options.enabled)
        {
            emit_profile(std::cerr, result.profile, false, profile_options.format);
        }
        return kExitError;
    }

    std::cout << "curlee run: result " << vm::to_string(result.value) << "\n";
    if (profile_options.enabled)
    {
        emit_profile(std::cout, result.profile, true, profile_options.format);
    }
    return kExitOk;
}

int cmd_bundle_build(const std::string& entry_path_arg, const std::string& bundle_path,
                     const curlee::runtime::Capabilities& requested_caps,
                     const std::vector<std::filesystem::path>& stdlib_roots,
                     const std::optional<std::filesystem::path>& input_root,
                     DiagOutputFormat diag_format)
{
    namespace fs = std::filesystem;

    fs::path entry_path = fs::path(entry_path_arg);
    if (input_root.has_value() && !entry_path.is_absolute()) // GCOVR_EXCL_LINE
    {
        entry_path = *input_root / entry_path;
    }

    std::vector<curlee::bundle::ImportPin> import_pins;
    std::vector<std::uint8_t> bytecode;
    const int rc =
        cmd_read_only("bundle-build", entry_path.string(), empty_caps(), kDefaultFuel, std::nullopt,
                      false, diag_format, {}, stdlib_roots, input_root, &import_pins, &bytecode);
    if (rc != kExitOk)
    {
        return rc;
    }

    if (const auto forbidden = find_forbidden_v1_capability(requested_caps); forbidden.has_value())
    {
        std::cerr << "error: capability not part of Curlee v1 surface: " << *forbidden << "\n";
        return kExitError;
    }

    std::vector<std::string> capabilities(requested_caps.begin(), requested_caps.end());
    std::sort(capabilities.begin(), capabilities.end());
    capabilities.erase(std::unique(capabilities.begin(), capabilities.end()), capabilities.end());

    curlee::bundle::Bundle bundle;
    bundle.manifest.capabilities = std::move(capabilities);
    bundle.manifest.imports = std::move(import_pins);
    bundle.manifest.proof = std::nullopt;
    bundle.bytecode = std::move(bytecode);

    const auto write_err = curlee::bundle::write_bundle(bundle_path, bundle);
    if (!write_err.message.empty())
    {
        std::cerr << "error: bundle build failed: " << write_err.message << "\n";
        return kExitError;
    }

    std::cout << "curlee bundle build: wrote " << bundle_path << "\n";
    return kExitOk;
}

int cmd_collect_dependency_snapshot(const std::string& entry_path_arg,
                                    const std::vector<std::filesystem::path>& stdlib_roots,
                                    const std::optional<std::filesystem::path>& input_root,
                                    std::string& out_entry_hash,
                                    std::vector<curlee::bundle::ImportPin>& out_import_pins,
                                    DiagOutputFormat diag_format)
{
    namespace fs = std::filesystem;

    fs::path entry_path = fs::path(entry_path_arg);
    if (input_root.has_value() && !entry_path.is_absolute())
    {
        entry_path = *input_root / entry_path;
    }

    std::vector<std::uint8_t> bytecode;
    const int rc = cmd_read_only("bundle-build", entry_path.string(), empty_caps(), kDefaultFuel,
                                 std::nullopt, false, diag_format, {}, stdlib_roots, input_root,
                                 &out_import_pins, &bytecode);
    if (rc != kExitOk)
    {
        return rc;
    }

    auto loaded = source::load_source_file(entry_path.string());
    if (auto* err = std::get_if<source::LoadError>(&loaded)) // GCOVR_EXCL_LINE
    {
        std::cerr << "error: dependency snapshot failed: " << err->message
                  << "\n"; // GCOVR_EXCL_LINE
        return kExitError; // GCOVR_EXCL_LINE
    }

    const auto& file = std::get<source::SourceFile>(loaded);
    out_entry_hash = hash_text(file.contents);
    return kExitOk;
}

int cmd_deps_lock(const std::string& entry_path_arg, const std::string& lock_path,
                  const std::vector<std::filesystem::path>& stdlib_roots,
                  const std::optional<std::filesystem::path>& input_root,
                  DiagOutputFormat diag_format)
{
    std::string entry_hash;
    std::vector<curlee::bundle::ImportPin> import_pins;
    const int rc = cmd_collect_dependency_snapshot(entry_path_arg, stdlib_roots, input_root,
                                                   entry_hash, import_pins, diag_format);
    if (rc != kExitOk)
    {
        return rc;
    }

    DependencyLockfile lock;
    lock.entry_hash = std::move(entry_hash);
    lock.imports = std::move(import_pins);

    std::string error;
    if (!write_dependency_lockfile(lock_path, lock, error))
    {
        std::cerr << "error: deps lock failed: " << error << "\n";
        return kExitError;
    }

    std::cout << "curlee deps lock: wrote " << lock_path << "\n";
    return kExitOk;
}

int cmd_deps_verify(const std::string& entry_path_arg, const std::string& lock_path,
                    const std::vector<std::filesystem::path>& stdlib_roots,
                    const std::optional<std::filesystem::path>& input_root,
                    DiagOutputFormat diag_format)
{
    std::string entry_hash;
    std::vector<curlee::bundle::ImportPin> import_pins;
    const int rc = cmd_collect_dependency_snapshot(entry_path_arg, stdlib_roots, input_root,
                                                   entry_hash, import_pins, diag_format);
    if (rc != kExitOk)
    {
        return rc;
    }

    DependencyLockfile lock;
    std::string error;
    if (!read_dependency_lockfile(lock_path, lock, error))
    {
        std::cerr << "error: deps verify failed: " << error << "\n";
        return kExitError;
    }

    if (entry_hash != lock.entry_hash)
    {
        std::cerr << "error: deps verify failed: entry hash mismatch\n";
        std::cerr << "  expected: " << lock.entry_hash << "\n";
        std::cerr << "  actual:   " << entry_hash << "\n";
        return kExitError;
    }

    if (!import_pins_equal(import_pins, lock.imports))
    {
        std::cerr << "error: deps verify failed: imports mismatch\n";
        std::cerr << "  expected: " << join_import_pins(lock.imports) << "\n";
        std::cerr << "  actual:   " << join_import_pins(import_pins) << "\n";
        return kExitError;
    }

    std::cout << "curlee deps verify: ok\n";
    return kExitOk;
}

std::optional<std::size_t> parse_size(std::string_view s)
{
    std::size_t out = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto res = std::from_chars(begin, end, out);
    if (res.ec != std::errc{} || res.ptr != end)
    {
        return std::nullopt;
    }
    return out;
}

std::optional<std::uint64_t> parse_u64(std::string_view s)
{
    std::uint64_t out = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto res = std::from_chars(begin, end, out);
    if (res.ec != std::errc{} || res.ptr != end)
    {
        return std::nullopt;
    }
    return out;
}

std::optional<DiagOutputFormat> parse_diag_output_format(std::string_view raw)
{
    if (raw == "text")
    {
        return DiagOutputFormat::Text;
    }
    if (raw == "json")
    {
        return DiagOutputFormat::Json;
    }
    return std::nullopt;
}

std::optional<ProfileOutputFormat> parse_profile_output_format(std::string_view raw)
{
    if (raw == "text")
    {
        return ProfileOutputFormat::Text;
    }
    if (raw == "json")
    {
        return ProfileOutputFormat::Json;
    }
    return std::nullopt;
}

int cmd_fmt(const std::string& path, bool check)
{
    std::string escaped = path;
    std::string::size_type pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos)
    {
        escaped.insert(pos, "\\");
        pos += 2;
    }

    std::string cmd = "clang-format -style=file ";
    if (check)
    {
        cmd += "--dry-run --Werror ";
    }
    else
    {
        cmd += "-i ";
    }
    cmd += "\"" + escaped + "\"";

    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        std::cerr << "error: clang-format failed\n";
        return kExitError;
    }
    return kExitOk;
}

// `curlee build [--target freestanding-c] [--link] [-o out] <entry.curlee>`
//
// Runs the full check pipeline (including verification) and either emits
// freestanding C (default) or, with --link, produces a bootable kernel ELF:
//
//   verify -> codegen to C -> cc -ffreestanding -fno-builtin -nostdlib -c ->
//   assemble crt0.S -> ld -nostdlib -T runtime/linker.ld -> kernel.elf
//
// On verification or codegen failure prints diagnostics and exits non-zero
// without writing an output file.
int cmd_build(const std::string& entry_path, const std::string& output_path,
              const std::vector<std::filesystem::path>& stdlib_roots, bool to_stdout, bool link,
              DiagOutputFormat diag_format)
{
    std::string c_source;
    const int rc =
        cmd_read_only("build", entry_path, empty_caps(), kDefaultFuel, std::nullopt, false,
                      diag_format, {}, stdlib_roots, std::nullopt, nullptr, nullptr, &c_source);
    if (rc != kExitOk)
    {
        return rc;
    }

    if (!link)
    {
        if (to_stdout)
        {
            std::cout << c_source;
            return kExitOk;
        }

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            std::cerr << "error: build failed: cannot open output file: " << output_path << "\n";
            return kExitError;
        }
        out << c_source;
        out.close();

        std::cout << "curlee build: wrote " << output_path << "\n";
        return kExitOk;
    }

    if (to_stdout)
    {
        std::cerr << "error: curlee build --link requires an output file (-o <kernel.elf>)\n";
        return kExitError;
    }

    namespace fs = std::filesystem;

    // Escape a path for a POSIX shell command line.
    auto shell_quote = [](const std::string& s)
    {
        if (s.find_first_of(" \t\n'\"\\$&;|<>()*?[]`~!#") == std::string::npos)
        {
            return s;
        }
        std::string out = "'";
        for (const char c : s)
        {
            if (c == '\'')
            {
                out += "'\\''";
            }
            else
            {
                out.push_back(c);
            }
        }
        out += "'";
        return out;
    };

    auto run_shell = [](const std::string& cmd) { return std::system(cmd.c_str()) == 0; };

    // Locate a runtime file (crt0.S / linker.ld / rt.c). Prefers the
    // compile-time CURLEE_RUNTIME_DIR; falls back to a relative `runtime/`
    // next to the binary.
    auto runtime_file = [&](std::string_view name) -> fs::path
    {
        std::error_code ec;
#ifdef CURLEE_RUNTIME_DIR
        fs::path from_build = fs::path(CURLEE_RUNTIME_DIR) / name;
        if (fs::exists(from_build, ec))
        {
            return from_build;
        }
#endif
        return fs::path("runtime") / name;
    };

    const fs::path crt0 = runtime_file("crt0.S");
    const fs::path linker_script = runtime_file("linker.ld");
    const fs::path rt_c = runtime_file("rt.c");
    if (!fs::exists(crt0))
    {
        std::cerr << "error: build --link: boot stub not found: " << crt0.string() << "\n";
        return kExitError;
    }
    if (!fs::exists(linker_script))
    {
        std::cerr << "error: build --link: linker script not found: " << linker_script.string()
                  << "\n";
        return kExitError;
    }

    // Scratch files live next to the output (same filesystem).
    fs::path workdir = fs::path(output_path).parent_path();
    if (workdir.empty())
    {
        workdir = ".";
    }
    const fs::path kernel_c = workdir / "curlee_kernel.c";
    const fs::path kernel_o = workdir / "curlee_kernel.o";
    const fs::path crt0_o = workdir / "curlee_crt0.o";
    const fs::path rt_o = workdir / "curlee_rt.o";

    auto fail = [&](const std::string& msg)
    {
        std::cerr << "error: build --link: " << msg << "\n";
        std::error_code ec;
        fs::remove(kernel_c, ec);
        fs::remove(kernel_o, ec);
        fs::remove(crt0_o, ec);
        fs::remove(rt_o, ec);
        return kExitError;
    };

    {
        std::ofstream out(kernel_c, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return fail("cannot open scratch file " + kernel_c.string());
        }
        out << c_source;
        out.close();
    }

    const std::string cc = std::getenv("CC") != nullptr ? std::getenv("CC") : "cc";
    const std::string as = std::getenv("AS") != nullptr ? std::getenv("AS") : "cc";
    const std::string ld = std::getenv("LD") != nullptr ? std::getenv("LD") : "ld";
    const fs::path rt_dir = runtime_file("rt.h").parent_path();

    // 1. Compile the generated kernel C freestanding. The kernel is compiled
    //    with -mno-sse -mno-sse2: the boot stub (crt0.S) never enables SSE
    //    (CR4.OSFXSR), and the emitted C is pure scalar code that never needs
    //    it — but gcc would otherwise use SSE instructions for 16-byte
    //    aligned `{0}` zero-fills of array locals (issue #278), which #UD
    //    without OSFXSR. Real kernels (e.g. Linux) compile with -mno-sse too.
    {
        const std::string cmd = shell_quote(cc) +
                                " -ffreestanding -fno-builtin -nostdlib -std=c11 "
                                "-mno-sse -mno-sse2 "
                                "-I" +
                                shell_quote(rt_dir.string()) + " -c " +
                                shell_quote(kernel_c.string()) + " -o " +
                                shell_quote(kernel_o.string());
        if (!run_shell(cmd))
        {
            return fail("C compile of generated kernel failed");
        }
    }

    // 2. Compile the freestanding runtime (rt.c) when present.
    if (fs::exists(rt_c))
    {
        const std::string cmd = shell_quote(cc) +
                                " -ffreestanding -fno-builtin -nostdlib -std=c11 "
                                "-mno-sse -mno-sse2 "
                                "-I" +
                                shell_quote(rt_dir.string()) + " -c " + shell_quote(rt_c.string()) +
                                " -o " + shell_quote(rt_o.string());
        if (!run_shell(cmd))
        {
            return fail("C compile of runtime failed");
        }
    }

    // 3. Assemble the boot stub (crt0.S).
    {
        const std::string cmd = shell_quote(as) + " -ffreestanding -fno-builtin -nostdlib -c " +
                                shell_quote(crt0.string()) + " -o " + shell_quote(crt0_o.string());
        if (!run_shell(cmd))
        {
            return fail("assembly of boot stub failed");
        }
    }

    // 4. Link with the linker script into a bootable ELF.
    {
        std::string objs = shell_quote(kernel_o.string());
        if (fs::exists(rt_o))
        {
            objs += " " + shell_quote(rt_o.string());
        }
        objs += " " + shell_quote(crt0_o.string());
        const std::string cmd = shell_quote(ld) + " -nostdlib -static -T " +
                                shell_quote(linker_script.string()) + " " + objs + " -o " +
                                shell_quote(output_path);
        if (!run_shell(cmd))
        {
            return fail("link of kernel ELF failed");
        }
    }

    std::cout << "curlee build: wrote " << output_path << "\n";
    return kExitOk;
}

std::vector<std::filesystem::path> load_stdlib_roots_from_env()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;

    if (const char* env_p = std::getenv("CURLEE_STDLIB_ROOT"))
    {
        std::string env_s(env_p);
        std::size_t start = 0;
        while (start < env_s.size())
        {
            std::size_t end = env_s.find(':', start);
            if (end == std::string::npos)
            {
                end = env_s.size();
            }

            if (end > start)
            {
                roots.push_back(fs::path(env_s.substr(start, end - start)));
            }

            start = end + 1;
        }
    }
    return roots;
} // GCOVR_EXCL_LINE

} // namespace

int run(int argc, char** argv)
{
    if (argc <= 1)
    {
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string_view first = argv[1];
    if (is_help_flag(first))
    {
        print_usage(std::cout);
        return kExitOk;
    }

    if (is_version_flag(first))
    {
        print_version(std::cout);
        return kExitOk;
    }

    // Python-style shorthand: `curlee path/to/file.curlee` is the same as `curlee run
    // path/to/file.curlee`.
    if (argc == 2 && !first.starts_with('-') && ends_with(first, ".curlee"))
    {
        return cmd_read_only("run", std::string(first), empty_caps(), kDefaultFuel, std::nullopt,
                             false, DiagOutputFormat::Text, {}, load_stdlib_roots_from_env());
    }

    const std::string_view cmd = argv[1];
    std::vector<std::string_view> args;
    for (int i = 2; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }

    DiagOutputFormat diag_format = DiagOutputFormat::Text;
    {
        std::vector<std::string_view> filtered_args;
        for (std::size_t i = 0; i < args.size();)
        {
            const std::string_view a = args[i];
            if (a == "--diag-format")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected format after --diag-format\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }

                const auto parsed = parse_diag_output_format(args[i + 1]);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected --diag-format to be one of: text, json\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                diag_format = *parsed;
                i += 2;
                continue;
            }

            if (a.starts_with("--diag-format="))
            {
                const auto raw = a.substr(std::string_view("--diag-format=").size());
                const auto parsed = parse_diag_output_format(raw);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected --diag-format= to be one of: text, json\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                diag_format = *parsed;
                ++i;
                continue;
            }

            filtered_args.push_back(a);
            ++i;
        }
        args = std::move(filtered_args);
    }

    if (cmd == "build")
    {
        std::string target = "freestanding-c";
        std::string output = "out.c";
        bool to_stdout = false;
        bool link = false;
        bool output_explicit = false;
        auto stdlib_roots = load_stdlib_roots_from_env();
        std::optional<std::filesystem::path> root;
        std::vector<std::string> positional;

        for (std::size_t i = 0; i < args.size();)
        {
            const std::string_view a = args[i];

            // `curlee build --help` prints the build usage and exits 0.
            if (a == "--help" || a == "-h")
            {
                print_build_usage(std::cout);
                return kExitOk;
            }

            // `--` ends option parsing so a file named like an option (or `-`)
            // can be built.
            if (a == "--")
            {
                for (++i; i < args.size(); ++i)
                {
                    positional.push_back(std::string(args[i]));
                }
                break;
            }

            if (a == "--target")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected target after --target\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                target = std::string(args[i + 1]);
                i += 2;
                continue;
            }

            if (a.starts_with("--target="))
            {
                const auto value = a.substr(std::string_view("--target=").size());
                if (value.empty())
                {
                    std::cerr << "error: expected target after --target=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                target = std::string(value);
                ++i;
                continue;
            }

            // `--link` produces a bootable kernel ELF (verify -> codegen -> cc
            // -> assemble crt0.S -> ld -T linker.ld) instead of plain C.
            if (a == "--link")
            {
                link = true;
                ++i;
                continue;
            }

            if (a == "-o" || a == "--output")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after " << a << "\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                output = std::string(args[i + 1]);
                to_stdout = (output == "-");
                output_explicit = true;
                i += 2;
                continue;
            }

            if (a.starts_with("--output="))
            {
                const auto value = a.substr(std::string_view("--output=").size());
                if (value.empty())
                {
                    std::cerr << "error: expected path after --output=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                output = std::string(value);
                to_stdout = (output == "-");
                output_explicit = true;
                ++i;
                continue;
            }

            if (a == "--root")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after --root\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                root = std::filesystem::path(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--root="))
            {
                const auto value = a.substr(std::string_view("--root=").size());
                if (value.empty())
                {
                    std::cerr << "error: expected path after --root=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                root = std::filesystem::path(std::string(value));
                ++i;
                continue;
            }

            if (a == "--stdlib-root")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after --stdlib-root\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--stdlib-root="))
            {
                const auto value = a.substr(std::string_view("--stdlib-root=").size());
                if (value.empty())
                {
                    std::cerr << "error: expected path after --stdlib-root=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(value));
                ++i;
                continue;
            }

            if (a.starts_with('-'))
            {
                std::cerr << "error: unknown option: " << a << "\n\n";
                print_usage(std::cerr);
                return kExitUsage;
            }

            positional.push_back(std::string(a));
            ++i;
        }

        if (positional.size() != 1)
        {
            std::cerr << "error: expected curlee build [--target freestanding-c] "
                         "[-o out] <entry.curlee>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        if (target != "freestanding-c")
        {
            std::cerr << "error: unsupported build target: " << target
                      << " (supported: freestanding-c)\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        // `--link` produces a kernel ELF: it needs an explicit output path so
        // the user cannot accidentally overwrite a default `out.c` with an ELF.
        if (link && !output_explicit)
        {
            std::cerr << "error: curlee build --link requires an output file "
                         "(-o <kernel.elf>)\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        std::string entry_path = positional[0];
        if (root.has_value() && !std::filesystem::path(entry_path).is_absolute())
        {
            entry_path = (*root / entry_path).string();
        }

        return cmd_build(entry_path, output, stdlib_roots, to_stdout, link, diag_format);
    }

    if (cmd == "fmt")
    {
        bool check = false;
        if (args.size() == 2 && args[0] == "--check")
        {
            check = true;
            args.erase(args.begin());
        }

        if (args.size() != 1)
        {
            std::cerr << "error: expected curlee fmt [--check] <file>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        return cmd_fmt(std::string(args[0]), check);
    }

    if (cmd == "bundle")
    {
        if (args.empty())
        {
            std::cerr << "error: expected curlee bundle <build|verify|info> ...\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        const std::string_view sub = args[0];

        if (sub == "build")
        {
            curlee::runtime::Capabilities caps;
            auto stdlib_roots = load_stdlib_roots_from_env();
            std::optional<std::filesystem::path> root;
            std::vector<std::string> positional;

            for (std::size_t i = 1; i < args.size();)
            {
                const std::string_view a = args[i]; // GCOVR_EXCL_LINE

                if (a == "--root")
                {
                    if (i + 1 >= args.size())
                    {
                        std::cerr << "error: expected path after --root\n\n"; // GCOVR_EXCL_LINE
                        print_usage(std::cerr);
                        return kExitUsage;
                    }
                    root = std::filesystem::path(std::string(args[i + 1]));
                    i += 2;
                    continue; // GCOVR_EXCL_LINE
                }

                if (a.starts_with("--root="))
                {
                    const auto root_value = a.substr(std::string_view("--root=").size());
                    if (root_value.empty()) // GCOVR_EXCL_LINE
                    {
                        std::cerr << "error: expected path after --root=\n\n";
                        print_usage(std::cerr);
                        return kExitUsage;
                    } // GCOVR_EXCL_LINE
                    root = std::filesystem::path(std::string(root_value)); // GCOVR_EXCL_LINE
                    ++i;                                                   // GCOVR_EXCL_LINE
                    continue;                                              // GCOVR_EXCL_LINE
                } // GCOVR_EXCL_LINE

                if (a == "--stdlib-root")
                {
                    if (i + 1 >= args.size()) // GCOVR_EXCL_LINE
                    {
                        std::cerr
                            << "error: expected path after --stdlib-root\n\n"; // GCOVR_EXCL_LINE
                        print_usage(std::cerr);
                        return kExitUsage;
                    }
                    stdlib_roots.push_back(std::string(args[i + 1])); // GCOVR_EXCL_LINE
                    i += 2;                                           // GCOVR_EXCL_LINE
                    continue;                                         // GCOVR_EXCL_LINE
                }

                if (a.starts_with("--stdlib-root="))
                {
                    const auto root_value = a.substr(std::string_view("--stdlib-root=").size());
                    if (root_value.empty()) // GCOVR_EXCL_LINE
                    {
                        std::cerr << "error: expected path after --stdlib-root=\n\n";
                        print_usage(std::cerr);
                        return kExitUsage;
                    }
                    stdlib_roots.push_back(std::string(root_value)); // GCOVR_EXCL_LINE
                    ++i;                                             // GCOVR_EXCL_LINE
                    continue;                                        // GCOVR_EXCL_LINE
                } // GCOVR_EXCL_LINE

                if (a == "--cap" || a == "--capability") // GCOVR_EXCL_LINE
                {
                    if (i + 1 >= args.size())
                    {
                        std::cerr << "error: expected capability name after " << a << "\n\n";
                        print_usage(std::cerr);
                        return kExitUsage;
                    }
                    caps.insert(std::string(args[i + 1]));
                    i += 2;
                    continue;
                }

                if (a.starts_with("--cap="))
                {
                    const auto cap = a.substr(std::string_view("--cap=").size());
                    if (cap.empty()) // GCOVR_EXCL_LINE
                    {
                        std::cerr << "error: expected capability name after --cap=\n\n";
                        print_usage(std::cerr);
                        return kExitUsage;
                    }
                    caps.insert(std::string(cap)); // GCOVR_EXCL_LINE
                    ++i;                           // GCOVR_EXCL_LINE
                    continue;                      // GCOVR_EXCL_LINE
                } // GCOVR_EXCL_LINE

                if (a.starts_with('-'))
                {
                    std::cerr << "error: unknown option: " << a << "\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }

                positional.push_back(std::string(a));
                ++i;
            }

            if (positional.size() != 2)
            {
                std::cerr << "error: expected curlee bundle build [options] <entry.curlee> "
                             "<out.bundle>\n\n";
                print_usage(std::cerr);
                return kExitUsage;
            }

            return cmd_bundle_build(positional[0], positional[1], caps, stdlib_roots, root,
                                    diag_format);
        }

        if (args.size() != 2)
        {
            std::cerr << "error: expected curlee bundle <verify|info> <file.bundle>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        const std::string path = std::string(args[1]);

        const auto loaded = curlee::bundle::read_bundle(path);
        if (auto* err = std::get_if<curlee::bundle::BundleError>(&loaded))
        {
            std::cerr << "error: bundle " << sub << " failed: " << err->message << "\n";
            return kExitError;
        }

        const auto& b = std::get<curlee::bundle::Bundle>(loaded);

        if (sub == "verify")
        {
            if (const auto forbidden = find_forbidden_v1_capability(b.manifest.capabilities);
                forbidden.has_value())
            {
                std::cerr << "error: bundle verify failed: capability not part of Curlee v1 "
                             "surface: "
                          << *forbidden << "\n";
                return kExitError;
            }

            const auto decoded = curlee::vm::decode_chunk(b.bytecode);
            if (const auto* decode_err = std::get_if<curlee::vm::ChunkDecodeError>(&decoded))
            {
                std::cerr << "error: bundle verify failed: invalid bundle bytecode: "
                          << decode_err->message << "\n";
                return kExitError;
            }

            const auto& chunk = std::get<curlee::vm::Chunk>(decoded);
            if (chunk_uses_python_call(chunk))
            {
                std::cerr << "error: bundle verify failed: bundle bytecode uses python interop "
                             "opcode not supported in Curlee v1\n";
                return kExitError;
            }

            std::cout << "curlee bundle verify: ok\n";
            return kExitOk;
        }

        if (sub == "info")
        {
            std::cout << "curlee bundle info:\n";
            std::cout << "format_version: " << b.manifest.format_version << "\n";
            std::cout << "bytecode_hash: " << b.manifest.bytecode_hash << "\n";
            std::cout << "capabilities: " << join_csv(b.manifest.capabilities) << "\n";
            std::cout << "imports: " << join_import_pins(b.manifest.imports) << "\n";
            std::cout << "proof: " << (b.manifest.proof.has_value() ? "present" : "none") << "\n";
            return kExitOk;
        }

        std::cerr << "error: unknown bundle subcommand: " << sub << "\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    if (cmd == "deps")
    {
        if (args.empty())
        {
            std::cerr << "error: expected curlee deps <lock|verify> ...\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        const std::string_view sub = args[0];
        if (sub != "lock" && sub != "verify")
        {
            std::cerr << "error: unknown deps subcommand: " << sub << "\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        auto stdlib_roots = load_stdlib_roots_from_env();
        std::optional<std::filesystem::path> root;
        std::vector<std::string> positional;

        for (std::size_t i = 1; i < args.size();)
        {
            const std::string_view a = args[i];
            if (a == "--root")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after --root\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                root = std::filesystem::path(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--root="))
            {
                const auto root_value = a.substr(std::string_view("--root=").size());
                if (root_value.empty())
                {
                    std::cerr << "error: expected path after --root=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                root = std::filesystem::path(std::string(root_value));
                ++i;
                continue;
            }

            if (a == "--stdlib-root")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after --stdlib-root\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--stdlib-root="))
            {
                const auto root_value = a.substr(std::string_view("--stdlib-root=").size());
                if (root_value.empty())
                {
                    std::cerr << "error: expected path after --stdlib-root=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(root_value));
                ++i;
                continue;
            }

            if (a.starts_with('-'))
            {
                std::cerr << "error: unknown option: " << a << "\n\n";
                print_usage(std::cerr);
                return kExitUsage;
            }

            positional.push_back(std::string(a));
            ++i;
        }

        if (positional.size() != 2)
        {
            std::cerr << "error: expected curlee deps " << sub
                      << " [options] <entry.curlee> <deps.lock>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        if (sub == "lock")
        {
            return cmd_deps_lock(positional[0], positional[1], stdlib_roots, root, diag_format);
        }
        return cmd_deps_verify(positional[0], positional[1], stdlib_roots, root, diag_format);
    }

    if (cmd == "run")
    {
        curlee::runtime::Capabilities caps;
        std::optional<std::string> bundle_path;
        std::optional<curlee::bundle::Bundle> bundle;
        std::optional<std::string> path;
        std::size_t fuel = kDefaultFuel;
        std::optional<std::uint64_t> seed;
        RuntimeProfileOptions profile_options;
        bool use_window_graphics_backend = false;
        auto stdlib_roots = load_stdlib_roots_from_env();

        for (std::size_t i = 0; i < args.size();)
        {
            const std::string_view a = args[i];
            if (a == "--stdlib-root")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected path after --stdlib-root\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--stdlib-root="))
            {
                const auto root = a.substr(std::string_view("--stdlib-root=").size());
                if (root.empty())
                {
                    std::cerr << "error: expected path after --stdlib-root=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                stdlib_roots.push_back(std::string(root));
                ++i;
                continue;
            }

            if (a == "--cap" || a == "--capability")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected capability name after " << a << "\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                caps.insert(std::string(args[i + 1]));
                i += 2;
                continue;
            }

            if (a.starts_with("--cap="))
            {
                const auto cap = a.substr(std::string_view("--cap=").size());
                if (cap.empty())
                {
                    std::cerr << "error: expected capability name after --cap=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                caps.insert(std::string(cap));
                ++i;
                continue;
            }

            if (a == "--bundle")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected bundle path after --bundle\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                if (bundle_path.has_value())
                {
                    std::cerr << "error: expected a single --bundle <file.bundle>\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                bundle_path = std::string(args[i + 1]);
                i += 2;
                continue;
            }

            if (a == "--graphics")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected backend name after --graphics\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                const std::string_view backend = args[i + 1];
                if (backend != "window")
                {
                    std::cerr << "error: unsupported graphics backend: " << backend << "\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                use_window_graphics_backend = true;
                i += 2;
                continue;
            }

            if (a == "--fuel")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected integer after --fuel\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                const auto parsed = parse_size(args[i + 1]);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected non-negative integer for --fuel\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                fuel = *parsed;
                i += 2;
                continue;
            }

            if (a == "--seed")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected integer after --seed\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                const auto parsed = parse_u64(args[i + 1]);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected non-negative integer for --seed\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                seed = *parsed;
                i += 2;
                continue;
            }

            if (a == "--profile")
            {
                profile_options.enabled = true;
                ++i;
                continue;
            }

            if (a == "--profile-format")
            {
                if (i + 1 >= args.size())
                {
                    std::cerr << "error: expected format after --profile-format\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }

                const auto parsed = parse_profile_output_format(args[i + 1]);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected --profile-format to be one of: text, json\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                profile_options.enabled = true;
                profile_options.format = *parsed;
                i += 2;
                continue;
            }

            if (a.starts_with("--profile-format="))
            {
                const auto raw = a.substr(std::string_view("--profile-format=").size());
                const auto parsed = parse_profile_output_format(raw);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected --profile-format= to be one of: text, json\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                profile_options.enabled = true;
                profile_options.format = *parsed;
                ++i;
                continue;
            }

            if (a.starts_with("--fuel="))
            {
                const auto raw = a.substr(std::string_view("--fuel=").size());
                const auto parsed = parse_size(raw);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected non-negative integer for --fuel=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                fuel = *parsed;
                ++i;
                continue;
            }

            if (a.starts_with("--seed="))
            {
                const auto raw = a.substr(std::string_view("--seed=").size());
                const auto parsed = parse_u64(raw);
                if (!parsed.has_value())
                {
                    std::cerr << "error: expected non-negative integer for --seed=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                seed = *parsed;
                ++i;
                continue;
            }

            if (a.starts_with("--bundle="))
            {
                const auto p = a.substr(std::string_view("--bundle=").size());
                if (p.empty())
                {
                    std::cerr << "error: expected bundle path after --bundle=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                if (bundle_path.has_value())
                {
                    std::cerr << "error: expected a single --bundle <file.bundle>\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                bundle_path = std::string(p);
                ++i;
                continue;
            }

            if (a.starts_with("--graphics="))
            {
                const auto backend = a.substr(std::string_view("--graphics=").size());
                if (backend.empty())
                {
                    std::cerr << "error: expected backend name after --graphics=\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                if (backend != "window")
                {
                    std::cerr << "error: unsupported graphics backend: " << backend << "\n\n";
                    print_usage(std::cerr);
                    return kExitUsage;
                }
                use_window_graphics_backend = true;
                ++i;
                continue;
            }

            if (a.starts_with('-'))
            {
                std::cerr << "error: unknown option: " << a << "\n\n";
                print_usage(std::cerr);
                return kExitUsage;
            }

            if (path.has_value())
            {
                std::cerr << "error: expected a single <file.curlee>\n\n";
                print_usage(std::cerr);
                return kExitUsage;
            }

            path = std::string(a);
            ++i;
        }

        if (!path.has_value())
        {
            std::cerr << "error: expected <file.curlee>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        if (bundle_path.has_value())
        {
            const auto loaded = curlee::bundle::read_bundle(*bundle_path);
            if (auto* err = std::get_if<curlee::bundle::BundleError>(&loaded))
            {
                std::cerr << "error: failed to load bundle: " << err->message << "\n";
                return kExitError;
            }
            bundle = std::get<curlee::bundle::Bundle>(loaded);
        }

        if (use_window_graphics_backend && !caps.contains("gfx.window")) // GCOVR_EXCL_LINE
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = "capability not granted: gfx.window";
            d.span = std::nullopt;
            diag::Related note;
            note.message =
                "grant it with: curlee run --graphics=window --cap gfx.window <file.curlee>";
            note.span = std::nullopt;
            d.notes.push_back(note);

            source::SourceFile pseudo_file;
            pseudo_file.path = path.value_or("<input>");
            pseudo_file.contents = "";
            emit_diagnostic(std::cerr, d, pseudo_file, diag_format);
            return kExitError;
        }

        if (bundle.has_value())
        {
            return cmd_run_bundle(*bundle, *path, caps, fuel, seed, use_window_graphics_backend,
                                  diag_format, profile_options);
        }

        return cmd_read_only(cmd, *path, caps, fuel, seed, use_window_graphics_backend, diag_format,
                             profile_options, stdlib_roots);
    }

    if (args.size() != 1)
    {
        std::cerr << "error: expected <command> <file.curlee>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string path(args[0]);
    return cmd_read_only(cmd, path, empty_caps(), kDefaultFuel, std::nullopt, false, diag_format,
                         {}, load_stdlib_roots_from_env());
}

} // namespace curlee::cli
