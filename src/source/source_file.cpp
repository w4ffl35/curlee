#include <curlee/source/source_file.h>
#include <fstream>
#include <sstream>

namespace curlee::source
{

LoadResult load_source_file(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        return LoadError{.message = "failed to open file"};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    if (!in.good() && !in.eof())
    {
        return LoadError{.message = "failed while reading file"};
    }

    return SourceFile{.path = path, .contents = buffer.str()};
}

} // namespace curlee::source
