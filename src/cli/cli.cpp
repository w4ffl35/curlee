#include <algorithm>
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

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

const curlee::runtime::Capabilities& empty_caps()
{
    static const curlee::runtime::Capabilities caps;
    return caps;
}

void print_usage(std::ostream& out)
{
    out << "curlee: verification-first language (early scaffold)\n\n";
    out << "usage:\n";
    out << "  curlee --help\n";
    out << "  curlee <file.curlee>\n";
    out << "  curlee lex <file.curlee>\n";
    out << "  curlee parse <file.curlee>\n";
    out << "  curlee check <file.curlee>\n";
    out << "  curlee run [--bundle <file.bundle>] [--cap <capability>]... <file.curlee>\n";
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
}

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
}

int cmd_read_only(std::string_view cmd, const std::string& path,
                  const curlee::runtime::Capabilities& granted_caps,
                  const curlee::bundle::Manifest* bundle_manifest)
{
    auto loaded = source::load_source_file(path);
    if (auto* err = std::get_if<source::LoadError>(&loaded))
    {
        const source::SourceFile pseudo_file{.path = path, .contents = ""};
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

        const std::optional<fs::path> entry_dir =
            file.path.empty() ? std::nullopt : std::optional{fs::path(file.path).parent_path()};

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
            if (importing_file.path.empty())
            {
                diag::Diagnostic d;
                d.severity = diag::Severity::Error;
                d.message = "imports require a source file path";
                d.span = imp.span;
                return d;
            }

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
            if (bundle_manifest != nullptr)
            {
                if (!entry_dir.has_value())
                {
                    diag::Diagnostic d;
                    d.severity = diag::Severity::Error;
                    d.message = "bundle mode imports require an entry file path";
                    d.span = imp.span;
                    return d;
                }
                // Bundle mode: no dynamic filesystem resolution. Resolve from a single, fixed root.
                roots.push_back(*entry_dir);
            }
            else
            {
                roots.push_back(fs::path(importing_file.path).parent_path());
                if (entry_dir.has_value() && (*entry_dir != roots.front()))
                {
                    roots.push_back(*entry_dir);
                }
            }

            std::optional<std::string> last_err;
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

                if (bundle_manifest != nullptr)
                {
                    std::vector<std::uint8_t> bytes;
                    bytes.reserve(dep_file.contents.size());
                    for (const char c : dep_file.contents)
                    {
                        bytes.push_back(static_cast<std::uint8_t>(c));
                    }
                    const std::string actual_hash = curlee::bundle::hash_bytes(bytes);

                    const auto it = std::find_if(bundle_manifest->imports.begin(),
                                                 bundle_manifest->imports.end(),
                                                 [&](const curlee::bundle::ImportPin& pin)
                                                 { return pin.path == import_name; });

                    if (it == bundle_manifest->imports.end())
                    {
                        diag::Diagnostic d;
                        d.severity = diag::Severity::Error;
                        d.message = "import not pinned: '" + import_name + "'";
                        d.span = imp.span;
                        d.notes.push_back(diag::Related{.message = "expected pin '" +
                                                                   import_name + ":" +
                                                                   actual_hash + "'",
                                                        .span = std::nullopt});
                        return d;
                    }

                    if (it->hash != actual_hash)
                    {
                        diag::Diagnostic d;
                        d.severity = diag::Severity::Error;
                        d.message = "import pin hash mismatch: '" + import_name + "'";
                        d.span = imp.span;
                        d.notes.push_back(diag::Related{.message = "expected hash " + it->hash,
                                                        .span = std::nullopt});
                        d.notes.push_back(diag::Related{.message = "actual hash " + actual_hash,
                                                        .span = std::nullopt});
                        return d;
                    }
                }

                ImportLoadResult ok{
                    .file = std::move(dep_file),
                    .path = module_path,
                    .key = normalize_path(module_path.string()),
                };
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

            fs::path expected_path = roots.empty()
                                        ? fs::path{}
                                        : roots.front();
            for (const auto& part : imp.path)
            {
                expected_path /= std::string(part);
            }
            expected_path += ".curlee";

            const std::string err_msg = last_err.has_value() ? *last_err : "failed to open file";
            d.notes.push_back(diag::Related{.message = "expected module at " +
                                                       expected_path.string() + " (" + err_msg +
                                                       ")",
                                            .span = std::nullopt});
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
            if (visiting.contains(key))
            {
                const diag::Diagnostic d{
                    .severity = diag::Severity::Error,
                    .message = "import cycle detected",
                    .span = std::nullopt,
                    .notes = {diag::Related{.message = "cycle involves " + mod_file.path,
                                            .span = std::nullopt}},
                };
                std::cerr << diag::render(d, stable_file);
                return false;
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
                if (stable_file.path.empty())
                {
                    const diag::Diagnostic d{
                        .severity = diag::Severity::Error,
                        .message = "imports require a source file path",
                        .span = imp.span,
                        .notes = {},
                    };
                    std::cerr << diag::render(d, stable_file);
                    visiting.erase(key);
                    return false;
                }

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
                    d.notes.push_back(diag::Related{
                        .message = "cycle involves " + dep_ok.path.string(), .span = std::nullopt});
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

            const auto resolved = resolver::resolve(mod_program, stable_file, entry_dir);
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
            if (!imported_by_path.contains(key))
            {
                imported_programs.push_back(std::move(mod_program));
                imported_by_path.emplace(key, imported_programs.size() - 1);
            }
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
        if (!file.path.empty())
        {
            visiting.insert(normalize_path(file.path));
        }
        for (const auto& imp : program.imports)
        {
            if (file.path.empty())
            {
                diag::Diagnostic d;
                d.severity = diag::Severity::Error;
                d.message = "imports require a source file path";
                d.span = imp.span;
                std::cerr << diag::render(d, file);
                return false;
            }
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
                d.notes.push_back(diag::Related{.message = "cycle involves " + dep_ok.path.string(),
                                                .span = std::nullopt});
                std::cerr << diag::render(d, file);
                return false;
            }

            if (!check_module(dep_ok.file, 1, check_module))
            {
                return false;
            }
        }
        if (!file.path.empty())
        {
            visiting.erase(normalize_path(file.path));
        }

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
                        d.notes.push_back(diag::Related{
                            .message = "conflict while importing " + key, .span = std::nullopt});
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
        const auto result = machine.run(chunk, 10000, granted_caps);
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

    // Python-style shorthand: `curlee path/to/file.curlee` is the same as `curlee run
    // path/to/file.curlee`.
    if (argc == 2 && !first.starts_with('-') && ends_with(first, ".curlee"))
    {
        return cmd_read_only("run", std::string(first), empty_caps(), nullptr);
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
            std::cout << "version: " << b.manifest.version << "\n";
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

        const curlee::bundle::Manifest* manifest =
            bundle.has_value() ? &bundle->manifest : nullptr;
        return cmd_read_only(cmd, *path, caps, manifest);
    }

    if (argc != 3)
    {
        std::cerr << "error: expected <command> <file.curlee>\n\n";
        print_usage(std::cerr);
        return kExitUsage;
    }

    const std::string path = argv[2];
    return cmd_read_only(cmd, path, empty_caps(), nullptr);
}

} // namespace curlee::cli
