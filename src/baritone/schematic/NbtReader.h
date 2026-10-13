#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace baritone::nbt {

// A parsed NBT value (Bedrock files are little-endian, Java ones big-endian).
struct Tag;
using Compound = std::map<std::string, Tag, std::less<>>;
using List = std::vector<Tag>;

struct Tag {
    std::variant<std::monostate, int64_t, double, std::string, std::vector<int64_t>, List, Compound> value;

    bool isCompound() const { return std::holds_alternative<Compound>(value); }
    const Compound* compound() const { return std::get_if<Compound>(&value); }
    const List* list() const { return std::get_if<List>(&value); }
    const std::string* string() const { return std::get_if<std::string>(&value); }
    std::optional<int64_t> integer() const {
        if (auto v = std::get_if<int64_t>(&value)) return *v;
        return std::nullopt;
    }
    const std::vector<int64_t>* intArray() const { return std::get_if<std::vector<int64_t>>(&value); }
    const Tag* get(std::string_view key) const;  // compound child
};

// Parses a whole file. Throws std::runtime_error on malformed data.
Tag parse(const std::vector<uint8_t>& data, bool littleEndian, std::string* rootName = nullptr);
std::vector<uint8_t> readFile(const std::string& path);

}  // namespace baritone::nbt
