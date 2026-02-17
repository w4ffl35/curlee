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

    return 0;
}
