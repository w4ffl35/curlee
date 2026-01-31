#include <curlee/source/source_file.h>
#include <fstream>
#include <sstream>

namespace curlee::source
{

LoadResult load_source_stream(std::istream& in, const std::string& path)
{
    std::ostringstream buffer;
    buffer << in.rdbuf();

    if (!in.good() && !in.eof())
    {
        return LoadError{.message = "failed while reading file"};
    }

    return SourceFile{.path = path, .contents = buffer.str()};
}

LoadResult load_source_file(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        return LoadError{.message = "failed to open file"};
    }

    return load_source_stream(in, path);
}

} // namespace curlee::source
