#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

static void fail(const std::string& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

static std::string trim(std::string s)
{
    const auto is_space = [](unsigned char c)
    { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };

    std::size_t start = 0;
    while (start < s.size() && is_space(static_cast<unsigned char>(s[start])))
    {
        ++start;
    }

    std::size_t end = s.size();
    while (end > start && is_space(static_cast<unsigned char>(s[end - 1])))
    {
        --end;
    }

    return s.substr(start, end - start);
}

static bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static std::optional<long long> parse_header_int(std::string_view line, std::string_view key)
{
    // Expected forms like: "# seed=1337" (leading/trailing whitespace ok).
    std::string s(line);
    s = trim(std::move(s));

    const std::string prefix = std::string("# ") + std::string(key) + "=";
    if (!starts_with(s, prefix))
    {
        return std::nullopt;
    }

    const std::string value = s.substr(prefix.size());
    if (value.empty())
    {
        fail("header key '" + std::string(key) + "' has empty value");
    }

    std::size_t consumed = 0;
    long long parsed = 0;
    try
    {
        parsed = std::stoll(value, &consumed, 10);
    }
    catch (const std::exception&)
    {
        fail("header key '" + std::string(key) + "' has non-integer value: '" + value + "'");
    }

    if (consumed != value.size())
    {
        fail("header key '" + std::string(key) + "' has trailing junk: '" + value + "'");
    }

    return parsed;
}

static std::optional<std::string> parse_header_str(std::string_view line, std::string_view key)
{
    std::string s(line);
    s = trim(std::move(s));

    const std::string prefix = std::string("# ") + std::string(key) + "=";
    if (!starts_with(s, prefix))
    {
        return std::nullopt;
    }

    return s.substr(prefix.size());
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fail("usage: curlee_training_data_tests <path/to/generator.py> <tmp_base_dir>");
    }

    namespace fs = std::filesystem;

    const fs::path generator = argv[1];
    const fs::path tmp_base = argv[2];
    const fs::path tmp_dir = tmp_base / ("training_data_test_tmp_" + std::to_string(::getpid()));
    const fs::path out_dir = tmp_dir / "correct_samples";
    const fs::path training_path = tmp_dir / "training_data.txt";

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec)
    {
        fail("failed to create temp output dir: " + out_dir.string());
    }

    // Generate a small dataset so CI stays fast.
    const int count_to_generate = 25;
    const int seed_to_generate = 1337;

    const std::string cmd = "python3 \"" + generator.string() + "\" --count " +
                            std::to_string(count_to_generate) + " --seed " +
                            std::to_string(seed_to_generate) + " --out \"" + out_dir.string() +
                            "\" --training \"" + training_path.string() + "\"";

    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        fail("generator failed (rc=" + std::to_string(rc) + "): " + cmd);
    }

    std::ifstream in(training_path);
    if (!in)
    {
        fail("failed to open generated dataset: " + training_path.string());
    }

    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        lines.push_back(std::move(line));
    }

    if (lines.empty())
    {
        fail("dataset file is empty");
    }

    if (trim(lines[0]) != "# Curlee correct_samples dataset")
    {
        fail("missing/incorrect dataset header title line");
    }

    std::optional<long long> seed;
    std::optional<long long> count;
    std::optional<long long> version;
    std::optional<std::string> generator_id;
    std::optional<std::string> bump_policy;

    std::size_t first_program_line = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string t = trim(lines[i]);
        if (t.empty() || t[0] != '#')
        {
            first_program_line = i;
            break;
        }

        if (!version)
        {
            version = parse_header_int(t, "dataset_version");
        }
        if (!generator_id)
        {
            generator_id = parse_header_str(t, "generator");
        }
        if (!bump_policy)
        {
            bump_policy = parse_header_str(t, "bump_dataset_version_when");
        }
        if (!seed)
        {
            seed = parse_header_int(t, "seed");
        }
        if (!count)
        {
            count = parse_header_int(t, "count");
        }

        // If file is all-header (shouldn't happen), loop continues.
        first_program_line = i + 1;
    }

    if (!version || *version <= 0)
    {
        fail("missing or invalid '# dataset_version=<int>' header");
    }
    if (!seed)
    {
        fail("missing '# seed=<int>' header");
    }
    if (!count || *count < 0)
    {
        fail("missing or invalid '# count=<int>' header");
    }
    if (!generator_id || generator_id->empty())
    {
        fail("missing or invalid '# generator=<string>' header");
    }
    if (!bump_policy || bump_policy->empty())
    {
        fail("missing or invalid '# bump_dataset_version_when=<string>' header");
    }

    std::vector<std::string> programs;
    std::string current;
    for (std::size_t i = first_program_line; i < lines.size(); ++i)
    {
        const std::string t = trim(lines[i]);
        if (t == "---")
        {
            if (trim(current).empty())
            {
                fail("found program separator '---' with empty program block");
            }
            programs.push_back(std::move(current));
            current.clear();
            continue;
        }

        current += lines[i];
        current += "\n";
    }

    if (!trim(current).empty())
    {
        programs.push_back(std::move(current));
    }

    if (*count != count_to_generate)
    {
        fail("generator declared count=" + std::to_string(*count) + " but expected " +
             std::to_string(count_to_generate));
    }
    if (*seed != seed_to_generate)
    {
        fail("generator declared seed=" + std::to_string(*seed) + " but expected " +
             std::to_string(seed_to_generate));
    }
    if (programs.size() != static_cast<std::size_t>(*count))
    {
        fail("declared count=" + std::to_string(*count) + " but found " +
             std::to_string(programs.size()) + " program blocks");
    }

    return 0;
}
