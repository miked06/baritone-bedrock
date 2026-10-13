#include "sdk/Nbt.h"

#include <format>

namespace sdk::nbt {

const CompoundTag* CompoundTag::compound(std::string_view key) const {
    const TagVariant* v = get(key);
    if (!v || v->type() != TagType::Compound) return nullptr;
    return reinterpret_cast<const CompoundTag*>(v->storage);
}

std::optional<std::string> CompoundTag::string(std::string_view key) const {
    const TagVariant* v = get(key);
    if (!v || v->type() != TagType::String) return std::nullopt;
    return *static_cast<const std::string*>(v->payload());
}

std::optional<int64_t> CompoundTag::integer(std::string_view key) const {
    const TagVariant* v = get(key);
    if (!v) return std::nullopt;
    switch (v->type()) {
        case TagType::Byte: return *static_cast<const uint8_t*>(v->payload());
        case TagType::Short: return *static_cast<const int16_t*>(v->payload());
        case TagType::Int: return *static_cast<const int32_t*>(v->payload());
        case TagType::Int64: return *static_cast<const int64_t*>(v->payload());
        default: return std::nullopt;
    }
}

std::string toString(const CompoundTag& tag, int depth) {
    if (depth > 4) return "{...}";
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : tag.tags) {
        if (!first) out += ", ";
        first = false;
        out += key + ": ";
        switch (value.type()) {
            case TagType::Byte: out += std::format("{}b", *static_cast<const uint8_t*>(value.payload())); break;
            case TagType::Short: out += std::format("{}s", *static_cast<const int16_t*>(value.payload())); break;
            case TagType::Int: out += std::format("{}", *static_cast<const int32_t*>(value.payload())); break;
            case TagType::Int64: out += std::format("{}L", *static_cast<const int64_t*>(value.payload())); break;
            case TagType::Float: out += std::format("{}f", *static_cast<const float*>(value.payload())); break;
            case TagType::Double: out += std::format("{}d", *static_cast<const double*>(value.payload())); break;
            case TagType::String: out += "\"" + *static_cast<const std::string*>(value.payload()) + "\""; break;
            case TagType::Compound:
                out += toString(*reinterpret_cast<const CompoundTag*>(value.storage), depth + 1);
                break;
            default: out += std::format("<type {}>", static_cast<int>(value.index)); break;
        }
    }
    return out + "}";
}

}  // namespace sdk::nbt
