// SPDX-License-Identifier: MIT
//
// Fuzz target for the bundle parser (read_bundle). Bundles are untrusted input
// by design (agents exchange bundles), so the decoder must be robust against
// arbitrary bytes.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <curlee/bundle/bundle.h>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string input(reinterpret_cast<const char*>(data), size);

    // read_bundle parses from a file path; write the input to a scratch file.
    constexpr const char* kScratch = "/tmp/curlee_bundle_fuzz.bundle";
    {
        std::ofstream out(kScratch, std::ios::binary | std::ios::trunc);
        if (out)
        {
            out.write(input.data(), static_cast<std::streamsize>(input.size()));
        }
    }

    (void)curlee::bundle::read_bundle(kScratch);
    std::remove(kScratch);

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
