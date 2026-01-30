#include <curlee/lexer/lexer.h>
#include <curlee/parser/parser.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  const std::string input(reinterpret_cast<const char*>(data), size);

  const auto lexed = curlee::lexer::lex(input);
  if(std::holds_alternative<curlee::diag::Diagnostic>(lexed))
  {
    return 0;
  }

  const auto& tokens = std::get<std::vector<curlee::lexer::Token>>(lexed);
  (void)curlee::parser::parse(tokens);

  return 0;
}

#ifdef CURLEE_FUZZER_STANDALONE
int main(int argc, char** argv)
{
  if(argc != 2)
  {
    return 2;
  }

  std::ifstream in(argv[1], std::ios::binary);
  if(!in)
  {
    return 2;
  }

  const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}
#endif
