#include "baritone/schematic/NbtReader.h"

#include <bit>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace baritone::nbt {

const Tag* Tag::get(std::string_view key) const {
    const Compound* c = compound();
    if (!c) return nullptr;
    auto it = c->find(key);
    return it == c->end() ? nullptr : &it->second;
}

namespace {

class Reader {
public:
    Reader(const std::vector<uint8_t>& data, bool little) : data_(data), little_(little) {}

    template <typename T>
    T read() {
        if (pos_ + sizeof(T) > data_.size()) throw std::runtime_error("NBT: unexpected end of file");
        T v;
        std::memcpy(&v, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        if (!little_) v = byteswap(v);
        return v;
    }

    std::string readString() {
        uint16_t len = read<uint16_t>();
        if (pos_ + len > data_.size()) throw std::runtime_error("NBT: truncated string");
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return s;
    }

    Tag readPayload(uint8_t type, int depth) {
        if (depth > 512) throw std::runtime_error("NBT: nesting too deep");
        Tag t;
        switch (type) {
            case 1: t.value = static_cast<int64_t>(read<int8_t>()); break;
            case 2: t.value = static_cast<int64_t>(read<int16_t>()); break;
            case 3: t.value = static_cast<int64_t>(read<int32_t>()); break;
            case 4: t.value = read<int64_t>(); break;
            case 5: t.value = static_cast<double>(std::bit_cast<float>(read<uint32_t>())); break;
            case 6: t.value = std::bit_cast<double>(read<uint64_t>()); break;
            case 7: {  // byte array
                int32_t n = read<int32_t>();
                checkCount(n, 1, sizeof(int64_t));
                std::vector<int64_t> v;
                v.reserve(n);
                for (int32_t i = 0; i < n; ++i) v.push_back(read<int8_t>());
                t.value = std::move(v);
                break;
            }
            case 8: t.value = readString(); break;
            case 9: {  // list
                uint8_t elemType = read<uint8_t>();
                int32_t n = read<int32_t>();
                static constexpr std::size_t minimum[] = {0, 1, 2, 4, 8, 4, 8, 4, 2, 5, 1, 4, 4};
                if (elemType > 12 || (elemType == 0 && n != 0)) throw std::runtime_error("NBT: invalid list type");
                checkCount(n, minimum[elemType], sizeof(Tag));
                List list;
                list.reserve(n > 0 ? n : 0);
                for (int32_t i = 0; i < n; ++i) list.push_back(readPayload(elemType, depth + 1));
                t.value = std::move(list);
                break;
            }
            case 10: {  // compound
                Compound c;
                while (true) {
                    uint8_t childType = read<uint8_t>();
                    if (childType == 0) break;
                    std::string name = readString();
                    c.emplace(std::move(name), readPayload(childType, depth + 1));
                }
                t.value = std::move(c);
                break;
            }
            case 11: {  // int array
                int32_t n = read<int32_t>();
                checkCount(n, 4, sizeof(int64_t));
                std::vector<int64_t> v;
                v.reserve(n);
                for (int32_t i = 0; i < n; ++i) v.push_back(read<int32_t>());
                t.value = std::move(v);
                break;
            }
            case 12: {  // long array
                int32_t n = read<int32_t>();
                checkCount(n, 8, sizeof(int64_t));
                std::vector<int64_t> v;
                v.reserve(n);
                for (int32_t i = 0; i < n; ++i) v.push_back(read<int64_t>());
                t.value = std::move(v);
                break;
            }
            default: throw std::runtime_error("NBT: unknown type " + std::to_string(type));
        }
        return t;
    }

private:
    void checkCount(int32_t n, std::size_t minimumBytes, std::size_t allocationBytes) {
        if (n < 0 || (minimumBytes && static_cast<std::size_t>(n) > (data_.size() - pos_) / minimumBytes))
            throw std::runtime_error("NBT: invalid or truncated collection length");
        const std::size_t count = static_cast<std::size_t>(n);
        if (allocationBytes && count > allocationBudget_ / allocationBytes)
            throw std::runtime_error("NBT: collection allocation limit exceeded");
        allocationBudget_ -= count * allocationBytes;
    }
    template <typename T>
    static T byteswap(T v) {
        if constexpr (sizeof(T) == 1) return v;
        else return std::bit_cast<T>(std::byteswap(std::bit_cast<std::conditional_t<
                                                       sizeof(T) == 2, uint16_t,
                                                       std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>(v)));
    }

    const std::vector<uint8_t>& data_;
    bool little_;
    std::size_t pos_ = 0;
    std::size_t allocationBudget_ = 512ull * 1024 * 1024;
};

}  // namespace

Tag parse(const std::vector<uint8_t>& data, bool littleEndian, std::string* rootName) {
    Reader r(data, littleEndian);
    uint8_t type = r.read<uint8_t>();
    if (type != 10) throw std::runtime_error("NBT: root is not a compound");
    std::string name = r.readString();
    if (rootName) *rootName = name;
    return r.readPayload(10, 0);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path);
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size < 0 || size > 256ll * 1024 * 1024) throw std::runtime_error("Schematic file exceeds the 256 MiB input limit");
    in.seekg(0);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace baritone::nbt
