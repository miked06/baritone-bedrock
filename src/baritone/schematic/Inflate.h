#pragma once
#include <cstdint>
#include <vector>

namespace baritone {

// DEFLATE decoder (RFC 1951), enough to read gzip'ed NBT files (.schem, .litematic,
// .schematic) without a zlib dependency. Throws std::runtime_error on corrupt data.
std::vector<uint8_t> inflateRaw(const uint8_t* data, std::size_t size, std::size_t* consumed = nullptr);

// gzip (RFC 1952) or zlib (RFC 1950) stream; anything else is returned unchanged.
std::vector<uint8_t> decompressIfNeeded(const std::vector<uint8_t>& data);

}  // namespace baritone
