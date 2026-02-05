#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <curlee/bundle/bundle.h>
#include <curlee/cli/cli.h>
#include <curlee/compiler/emitter.h>
#include <curlee/diag/render.h>
#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>
#include <curlee/resolver/resolver.h>
#include <curlee/source/source_file.h>
#include <curlee/types/type_check.h>
#include <curlee/verification/checker.h>
#include <curlee/vm/chunk_codec.h>
#include <curlee/vm/value.h>
#include <curlee/vm/vm.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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
    out << "  curlee run [--fuel <n>] [--bundle <file.bundle>] [--cap <capability>]... "
           "<file.curlee>\n";
    out << "  curlee fmt [--check] <file>\n";
    out << "  curlee bundle verify <file.bundle>\n";
    out << "  curlee bundle info <file.bundle>\n";
}

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

int cmd_read_only(std::string_view cmd, const std::string& path,
                  const curlee::runtime::Capabilities& granted_caps, std::size_t fuel)
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
        };

        std::cerr << diag::render(diag, pseudo_file);
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

    auto run_checks = [&](parser::Program& program) -> bool
    {
        namespace fs = std::filesystem;

        constexpr int kMaxImportDepth = 64;

        auto normalize_path = [](const std::string& p) -> std::string
        { return fs::path(p).lexically_normal().string(); };

        const fs::path entry_dir = fs::path(file.path).parent_path();

        imported_files.clear();
        imported_file_by_path.clear();
        imported_programs.clear();
        imported_by_path.clear();

        std::unordered_set<std::string> visiting;
        std::unordered_set<std::string> visited;
        std::vector<std::string> merge_order;

        auto render_diags = [](const std::vector<diag::Diagnostic>& ds,
                               const source::SourceFile& f) -> void
        {
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, f);
            }
        };

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
            std::string import_name;
            for (std::size_t i = 0; i < imp.path.size(); ++i)
            {
                import_name += std::string(imp.path[i]);
                if (i + 1 < imp.path.size())
                {
                    import_name += ".";
                }
            }

            std::vector<fs::path> roots;
            roots.push_back(fs::path(importing_file.path).parent_path());
            if (entry_dir != roots.front())
            {
                roots.push_back(entry_dir);
            }

            std::string last_err = "failed to open file";
            for (const auto& root : roots)
            {
                fs::path module_path = root;
                for (const auto& part : imp.path)
                {
                    module_path /= std::string(part);
                }
                module_path += ".curlee";

                if (std::getenv("CURLEE_DEBUG_IMPORTS") != nullptr)
                {
                    std::cerr << "[import] trying " << module_path.string() << "\n";
                }

                const auto loaded = source::load_source_file(module_path.string());
                if (auto* err = std::get_if<source::LoadError>(&loaded))
                {
                    if (std::getenv("CURLEE_DEBUG_IMPORTS") != nullptr)
                    {
                        std::cerr << "[import] failed: " << err->message << "\n";
                    }
                    last_err = err->message;
                    continue;
                }

                auto dep_file = std::get<source::SourceFile>(loaded);

                ImportLoadResult ok;
                ok.file = std::move(dep_file);
                ok.path = module_path;
                ok.key = normalize_path(module_path.string());
                if (std::getenv("CURLEE_DEBUG_IMPORTS") != nullptr)
                {
                    std::cerr << "[import] ok: " << ok.path.string() << "\n";
                }
                return ok;
            }

            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = "import not found: '" + import_name + "'";
            d.span = imp.span;

            fs::path expected_path = roots.front();
            for (const auto& part : imp.path)
            {
                expected_path /= std::string(part);
            }
            expected_path += ".curlee";

            const diag::Related note{.message = "expected module at " + expected_path.string() +
                                                " (" + last_err + ")",
                                     .span = std::nullopt};
            d.notes.push_back(note); // GCOVR_EXCL_LINE
            return d;
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
                };
                std::cerr << diag::render(d, mod_file);
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

            if (visited.contains(key))
            {
                return true;
            }

            visiting.insert(key);

            const auto lexed = lexer::lex(stable_file.contents);
            if (std::holds_alternative<diag::Diagnostic>(lexed))
            {
                std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), stable_file);
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
                    };
                    std::cerr << diag::render(d, stable_file);
                    visiting.erase(key);
                    return false;
                }
            }

            for (const auto& imp : mod_program.imports)
            {
                const auto dep_loaded = load_import(stable_file, imp);
                if (auto* d = std::get_if<diag::Diagnostic>(&dep_loaded))
                {
                    std::cerr << diag::render(*d, stable_file);
                    visiting.erase(key);
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
                    std::cerr << diag::render(d, stable_file);
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
                resolver::resolve(mod_program, stable_file, std::optional{entry_dir});
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

            const auto& type_info = std::get<types::TypeInfo>(typed);
            const auto verified = verification::verify(mod_program, type_info);
            if (std::holds_alternative<std::vector<diag::Diagnostic>>(verified))
            {
                render_diags(std::get<std::vector<diag::Diagnostic>>(verified), stable_file);
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
        };

        const auto lexed = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(lexed))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), file);
            return false;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
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
                std::cerr << diag::render(*d, file);
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
                std::cerr << diag::render(d, file);
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
                        std::cerr << diag::render(d, file);
                        return false;
                    }
                    seen.insert(name);
                    program.functions.push_back(std::move(f));
                }
            }

            // Expression IDs are per-parse; after merging we must make them unique.
            parser::reassign_expr_ids(program);
        }

        const auto resolved = resolver::resolve(program, file, entry_dir);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(resolved))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(resolved);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        const auto typed = types::type_check(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(typed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(typed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        const auto& type_info = std::get<types::TypeInfo>(typed);
        const auto verified = verification::verify(program, type_info);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(verified))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(verified);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return false;
        }

        return true;
    };

    if (cmd == "lex")
    {
        const auto res = lexer::lex(file.contents);
        if (std::holds_alternative<diag::Diagnostic>(res))
        {
            std::cerr << diag::render(std::get<diag::Diagnostic>(res), file);
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
            std::cerr << diag::render(std::get<diag::Diagnostic>(lexed), file);
            return kExitError;
        }

        const auto& toks = std::get<std::vector<lexer::Token>>(lexed);
        const auto parsed = parser::parse(toks);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(parsed))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(parsed);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
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

    if (cmd == "run")
    {
        parser::Program program;
        if (!run_checks(program))
        {
            return kExitError;
        }

        const auto emitted = compiler::emit_bytecode(program);
        if (std::holds_alternative<std::vector<diag::Diagnostic>>(emitted))
        {
            const auto& ds = std::get<std::vector<diag::Diagnostic>>(emitted);
            for (const auto& d : ds)
            {
                std::cerr << diag::render(d, file);
            }
            return kExitError;
        }

        const auto& chunk = std::get<vm::Chunk>(emitted);
        vm::VM machine;
        const auto result = machine.run(chunk, fuel, granted_caps);
        if (!result.ok)
        {
            diag::Diagnostic d;
            d.severity = diag::Severity::Error;
            d.message = result.error;
            d.span = result.error_span;
            std::cerr << diag::render(d, file);
            return kExitError;
        }

        std::cout << "curlee run: result " << vm::to_string(result.value) << "\n";
        return kExitOk;
    }

    std::cerr << "error: unknown command: " << cmd << "\n";
    return kExitUsage;
}

int cmd_run_bundle(const curlee::bundle::Bundle& bundle, const std::string& entry_path,
                   const curlee::runtime::Capabilities& granted_caps, std::size_t fuel)
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
        };

        std::cerr << diag::render(diag, pseudo_file);
        return kExitError;
    }

    const auto& file = std::get<source::SourceFile>(loaded);

    curlee::runtime::Capabilities effective_caps;
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
            std::cerr << diag::render(d, file);
            return kExitError;
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
        };
        std::cerr << diag::render(d, file);
        return kExitError;
    }

    const auto& chunk = std::get<curlee::vm::Chunk>(decoded);
    vm::VM machine;

    const auto result = machine.run(chunk, fuel, effective_caps);
    if (!result.ok)
    {
        diag::Diagnostic d;
        d.severity = diag::Severity::Error;
        d.message = result.error;
        d.span = result.error_span;
        std::cerr << diag::render(d, file);
        return kExitError;
    }

    std::cout << "curlee run: result " << vm::to_string(result.value) << "\n";
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
        return cmd_read_only("run", std::string(first), empty_caps(), kDefaultFuel);
    }

    const std::string_view cmd = argv[1];
    std::vector<std::string_view> args;
    for (int i = 2; i < argc; ++i)
    {
        args.push_back(argv[i]);
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
        if (args.size() != 2)
        {
            std::cerr << "error: expected curlee bundle <verify|info> <file.bundle>\n\n";
            print_usage(std::cerr);
            return kExitUsage;
        }

        const std::string_view sub = args[0];
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

    if (cmd == "run")
    {
        curlee::runtime::Capabilities caps;
        std::optional<std::string> bundle_path;
        std::optional<curlee::bundle::Bundle> bundle;
        std::optional<std::string> path;
        std::size_t fuel = kDefaultFuel;

        for (std::size_t i = 0; i < args.size();)
        {
            const std::string_view a = args[i];
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

        if (bundle.has_value())
        {
            return cmd_run_bundle(*bundle, *path, caps, fuel);
        }

        return cmd_read_only(cmd, *path, caps, fuel);
    }

    if (argc != 3)
    {
        std::cerr << "error: expected <command> <file.curlee>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string path = argv[2];
    return cmd_read_only(cmd, path, empty_caps(), kDefaultFuel);
}

} // namespace curlee::cli
