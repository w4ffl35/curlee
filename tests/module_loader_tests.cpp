#include <curlee/resolver/module_loader.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

[[noreturn]] static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static fs::path make_temp_dir(const std::string& stem)
{
    const auto pid = static_cast<unsigned long>(::getpid());
    const fs::path dir = fs::temp_directory_path() / "curlee_module_loader_tests" /
                         (stem + "_" + std::to_string(pid));

    std::error_code ec;
    fs::remove_all(dir, ec);
    ec = {};
    fs::create_directories(dir, ec);
    if (ec)
    {
        fail("failed to create temp dir: " + dir.string() + ": " + ec.message());
    }
    return dir;
}

static void write_file(const fs::path& path, std::string_view contents)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        fail("failed to create dirs for: " + path.string() + ": " + ec.message());
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        fail("failed to open temp file: " + path.string());
    }
    f << contents;
    if (!f)
    {
        fail("failed to write temp file: " + path.string());
    }
}

int main()
{
    // Resolve from stdlib roots (and execute debug trace path).
    {
        const fs::path dir = make_temp_dir("resolve_from_stdlib");
        const fs::path importing = dir / "workspace";
        const fs::path entry = dir / "entry";
        const fs::path stdlib_root = dir / "stdlib";
        const fs::path stdlib_mod = stdlib_root / "std" / "math.curlee";

        write_file(stdlib_mod, "fn add1(x: Int) -> Int { return x + 1; }\n");

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = importing;
        opts.entry_dir = entry;
        opts.stdlib_roots = {stdlib_root};
        opts.allow_stdlib = true;
        opts.debug_trace = true;

        const std::vector<std::string_view> import_path = {"std", "math"};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (!got)
        {
            fail("expected stdlib module to resolve");
        }

        if (!fs::exists(got->path))
        {
            fail("resolved path does not exist: " + got->path.string());
        }
        if (got->canonical_path.empty())
        {
            fail("expected canonical path to be non-empty");
        }
    }

    // Resolve from importing_file_dir when allow_stdlib is disabled.
    {
        const fs::path dir = make_temp_dir("resolve_from_local_only");
        const fs::path importing = dir / "workspace";
        const fs::path entry = dir / "entry";
        const fs::path local_mod = importing / "local" / "util.curlee";

        write_file(local_mod, "fn id(x: Int) -> Int { return x; }\n");

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = importing;
        opts.entry_dir = entry;
        opts.stdlib_roots = {dir / "stdlib"};
        opts.allow_stdlib = false;
        opts.debug_trace = false;

        const std::vector<std::string_view> import_path = {"local", "util"};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (!got)
        {
            fail("expected local module to resolve with allow_stdlib=false");
        }
    }

    // Not found reports deterministic expected path and stdlib hint.
    {
        const fs::path dir = make_temp_dir("resolve_missing_stdlib_hint");
        const fs::path importing = dir / "workspace";
        const fs::path entry = dir / "entry";
        const fs::path stdlib_root = dir / "stdlib";

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = importing;
        opts.entry_dir = entry;
        opts.stdlib_roots = {stdlib_root};
        opts.allow_stdlib = true;
        opts.debug_trace = true;

        const std::vector<std::string_view> import_path = {"missing", "module"};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (got)
        {
            fail("expected missing module lookup to fail");
        }

        const auto& d = got.error();
        if (d.message.find("import not found:") == std::string::npos)
        {
            fail("expected not-found diagnostic message");
        }
        if (d.notes.empty() || d.notes.front().message.find("or stdlib roots") == std::string::npos)
        {
            fail("expected stdlib-roots hint in diagnostic note");
        }
    }

    // Filesystem access error branch is handled deterministically.
    {
        const fs::path dir = make_temp_dir("resolve_permission_error");
        const fs::path blocked = dir / "blocked";
        const fs::path importing = blocked;

        std::error_code ec;
        fs::create_directories(blocked / "pkg", ec);
        if (ec)
        {
            fail("failed to create blocked dirs: " + ec.message());
        }

        fs::permissions(blocked, fs::perms::none, fs::perm_options::replace, ec);
        if (ec)
        {
            fail("failed to chmod blocked dir: " + ec.message());
        }

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = importing;
        opts.entry_dir = importing;
        opts.allow_stdlib = false;
        opts.debug_trace = true;

        const std::vector<std::string_view> import_path = {"pkg", "mod"};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);

        fs::permissions(blocked, fs::perms::owner_all, fs::perm_options::replace, ec);
        if (ec)
        {
            fail("failed to restore blocked dir permissions: " + ec.message());
        }

        if (got)
        {
            fail("expected lookup to fail when filesystem reports access error");
        }
    }

    // Oversized path component triggers filesystem error_code branch.
    {
        const fs::path dir = make_temp_dir("resolve_enametoolong");

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = dir;
        opts.entry_dir = dir;
        opts.allow_stdlib = false;
        opts.debug_trace = true;

        const std::string huge(5000, 'x');
        const std::vector<std::string_view> import_path = {huge};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (got)
        {
            fail("expected oversized path lookup to fail");
        }
    }

    // NUL-containing path component should trigger filesystem error handling.
    {
        const fs::path dir = make_temp_dir("resolve_invalid_component");

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = dir;
        opts.entry_dir = dir;
        opts.allow_stdlib = false;
        opts.debug_trace = true;

        const std::string invalid_part("bad\0name", 8);
        const std::vector<std::string_view> import_path = {invalid_part};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (got)
        {
            fail("expected invalid component lookup to fail");
        }
    }

    // Ambiguous import diagnostics should be deterministic regardless of root declaration order.
    {
        const fs::path dir = make_temp_dir("ambiguous_deterministic_order");
        const fs::path importing = dir / "workspace";
        const fs::path entry = importing;
        const fs::path root_a = dir / "root_a";
        const fs::path root_b = dir / "root_b";

        write_file(root_a / "pkg" / "util.curlee", "fn one() -> Int { return 1; }\n");
        write_file(root_b / "pkg" / "util.curlee", "fn one() -> Int { return 2; }\n");

        curlee::resolver::ModuleSearchOptions opts;
        opts.importing_file_dir = importing;
        opts.entry_dir = entry;
        opts.allow_stdlib = true;
        opts.stdlib_roots = {root_b, root_a};

        const std::vector<std::string_view> import_path = {"pkg", "util"};
        const auto got = curlee::resolver::resolve_module_path(import_path, opts);
        if (got)
        {
            fail("expected ambiguous import to fail");
        }

        const auto& d = got.error();
        if (d.message.find("ambiguous import") == std::string::npos)
        {
            fail("expected ambiguous import diagnostic message");
        }

        const std::string a_path = fs::absolute(root_a / "pkg" / "util.curlee").string();
        const std::string b_path = fs::absolute(root_b / "pkg" / "util.curlee").string();
        std::size_t a_index = std::string::npos;
        std::size_t b_index = std::string::npos;

        for (std::size_t i = 0; i < d.notes.size(); ++i)
        {
            if (d.notes[i].message.find(a_path) != std::string::npos)
            {
                a_index = i;
            }
            if (d.notes[i].message.find(b_path) != std::string::npos)
            {
                b_index = i;
            }
        }

        if (a_index == std::string::npos || b_index == std::string::npos)
        {
            fail("expected ambiguous import notes to contain both matching module paths");
        }
        if (!(a_index < b_index))
        {
            fail("expected ambiguous import notes to be sorted deterministically");
        }
    }

    return 0;
}
