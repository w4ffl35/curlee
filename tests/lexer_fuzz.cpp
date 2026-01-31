#include <cstddef>
#include <cstdint>
#include <curlee/lexer/lexer.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string input(reinterpret_cast<const char*>(data), size);

    // Determinism/capability rule: lexer must be driven only by input bytes.
    // We intentionally ignore the result; the fuzzer is looking for crashes/UB.
    (void)curlee::lexer::lex(input);

    return 0;
}

#ifdef CURLEE_FUZZER_STANDALONE
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in)
    {
        return 2;
    }

    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
    return LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                  bytes.size());
}
#endif
