#pragma once
// Read-only view of the game's NBT (CompoundTag), used to read block states
// (Block::mSerializationId = {"name", "states": {...}, "version"}).
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace sdk::nbt {

enum class TagType : uint8_t {
    End = 0, Byte = 1, Short = 2, Int = 3, Int64 = 4, Float = 5, Double = 6, ByteArray = 7, String = 8, List = 9,
    Compound = 10, IntArray = 11,
};

// std::variant<EndTag, ByteTag, ..., IntArrayTag>: every alternative starts with the Tag vtable,
// the payload follows at +8. The active index is stored after the largest alternative.
struct TagVariant {
    unsigned char storage[0x28];
    uint8_t index;
    unsigned char pad[7];

    TagType type() const { return static_cast<TagType>(index); }
    const void* payload() const { return storage + 8; }
};
static_assert(sizeof(TagVariant) == 0x30);

// A CompoundTag object: vtable + std::map<std::string, TagVariant>.
struct CompoundTag {
    void* vtable;
    std::map<std::string, TagVariant, std::less<>> tags;

    const TagVariant* get(std::string_view key) const {
        auto it = tags.find(key);
        return it == tags.end() ? nullptr : &it->second;
    }
    const CompoundTag* compound(std::string_view key) const;
    std::optional<std::string> string(std::string_view key) const;
    // Byte, Short or Int as an integer
    std::optional<int64_t> integer(std::string_view key) const;
};

// Human readable dump (debug).
std::string toString(const CompoundTag& tag, int depth = 0);

}  // namespace sdk::nbt
