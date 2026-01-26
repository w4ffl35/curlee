#pragma once

#include <string>
#include <variant>

namespace curlee::source
{

struct SourceFile
{
    std::string path;
    std::string contents;
};

struct LoadError
{
    std::string message;
};

using LoadResult = std::variant<SourceFile, LoadError>;

LoadResult load_source_file(const std::string& path);

} // namespace curlee::source
