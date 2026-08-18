// SPDX-License-Identifier: MIT
#include <curlee/resolver/module_loader.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace curlee::resolver
{

[[nodiscard]] curlee::SingleResult<ResolvedModule>
resolve_module_path(const std::vector<std::string_view>& import_path,
                    const ModuleSearchOptions& options)
{
    namespace fs = std::filesystem;

    std::string import_name;
    for (std::size_t i = 0; i < import_path.size(); ++i)
    {
        import_name += std::string(import_path[i]);
        if (i + 1 < import_path.size())
        {
            import_name += ".";
        }
    }

    // Roots resolution (Precedence: Local > Entry > Stdlib)
    std::vector<fs::path> roots;

    // 1. Importing file's directory (relative import).
    // Always included.
    roots.push_back(options.importing_file_dir);

    // 2. Entry file's directory (if different).
    if (!options.entry_dir.empty() && options.entry_dir != options.importing_file_dir)
    {
        roots.push_back(options.entry_dir);
    }

    // 3. Explicit stdlib roots (if configured and allowed).
    if (options.allow_stdlib)
    {
        for (const auto& root : options.stdlib_roots)
        {
            roots.push_back(root);
        }
    }

    std::vector<fs::path> matches;
    for (const auto& root : roots)
    {
        fs::path module_path = root;
        for (const auto& part : import_path)
        {
            module_path /= std::string(part);
        }
        module_path += ".curlee";

        if (options.debug_trace)
        {
            std::cerr << "[import] trying " << module_path.string() << "\n";
        }

        std::error_code ec;
        const bool exists = fs::exists(module_path, ec);
        if (ec)
        {
            std::cerr << "[import] failed: not found\n";
            continue;
        }

        if (exists)
        {
            matches.push_back(fs::absolute(module_path));
            if (options.debug_trace)
            {
                std::cerr << "[import] ok: " << fs::absolute(module_path).string() << "\n";
            }
        }
        else if (options.debug_trace)
        {
            std::cerr << "[import] failed: not found\n";
        }
    }

    if (matches.size() == 1)
    {
        ResolvedModule res;
        res.path = matches[0];
        res.canonical_path = fs::canonical(matches[0]).string();
        return res;
    }

    if (matches.size() > 1)
    {
        curlee::diag::Diagnostic d;
        d.severity = curlee::diag::Severity::Error;
        d.message = "ambiguous import: '" + import_name + "'";

        for (const auto& match : matches)
        {
            const curlee::diag::Related note{
                .message = "found module at " + match.string(),
                .span = std::nullopt,
            };
            d.notes.push_back(note);
        }

        const curlee::diag::Related hint{
            .message = "remove/rename one of the modules so the import is unambiguous",
            .span = std::nullopt,
        };
        d.notes.push_back(hint);
        return std::unexpected(d);
    }

    fs::path expected_file = options.importing_file_dir;
    for (const auto& part : import_path)
    {
        expected_file /= std::string(part);
    }
    expected_file += ".curlee";

    std::string expected_msg = "expected module at " + expected_file.string();
    // Add detail about stdlib if searched
    if (options.allow_stdlib && !options.stdlib_roots.empty())
    {
        expected_msg += " or stdlib roots";
    }

    curlee::diag::Diagnostic d;
    d.severity = curlee::diag::Severity::Error;
    d.message = "import not found: '" + import_name + "'";
    const curlee::diag::Related note{.message = expected_msg, .span = std::nullopt};
    d.notes.push_back(note);
    return std::unexpected(d);
}

} // namespace curlee::resolver
