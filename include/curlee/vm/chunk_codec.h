#pragma once

#include <cstdint>
#include <curlee/vm/bytecode.h>
#include <string>
#include <variant>
#include <vector>

/**
 * @file chunk_codec.h
 * @brief Stable encoding/decoding of VM chunks for bundle payloads.
 */

namespace curlee::vm
{

struct ChunkDecodeError
{
    std::string message;
};

/**
 * @brief Encode a VM Chunk into a stable byte representation.
 *
 * Format (little-endian):
 * - magic: "CURLEE_CHUNK" (11 bytes)
 * - u32 chunk_format_version
 *
 * Version 1 (legacy):
 * - u32 max_locals
 * - u32 code_len, then code bytes
 * - u32 spans_len, then spans: (u32 start, u32 end) repeated
 * - u32 constants_len, then constants:
 *     - u8 kind (0=int,1=bool,2=string,3=unit)
 *     - payload depending on kind
 *
 * Version 2 (current):
 * - u64 max_locals
 * - u64 code_len, then code bytes
 * - u64 spans_len, then spans: (u64 start, u64 end) repeated
 * - u64 constants_len, then constants:
 *     - u8 kind (0=int,1=bool,2=string,3=unit)
 *     - payload depending on kind
 *     - string payload is: u64 len, then bytes
 */
[[nodiscard]] std::vector<std::uint8_t> encode_chunk(const Chunk& chunk);

/** @brief Decode a Chunk previously produced by encode_chunk. */
[[nodiscard]] std::variant<Chunk, ChunkDecodeError>
decode_chunk(const std::vector<std::uint8_t>& bytes);

} // namespace curlee::vm