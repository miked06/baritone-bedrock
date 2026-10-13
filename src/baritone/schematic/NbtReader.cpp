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
        if (pos_ + sizeof(T) > data_.size()) throw std::runtime_error("NBT: fin de fichier inattendue");
        T v;
        std::memcpy(&v, data_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        if (!little_) v = byteswap(v);
        return v;
    }

    std::string readString() {
        uint16_t len = read<uint16_t>();
        if (pos_ + len > data_.size()) throw std::runtime_error("NBT: chaine tronquee");
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return s;
    }

    Tag readPayload(uint8_t type, int depth) {
        if (depth > 512) throw std::runtime_error("NBT: trop imbrique");
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
                std::vector<int64_t> v;
                v.reserve(n);
                for (int32_t i = 0; i < n; ++i) v.push_back(read<int32_t>());
                t.value = std::move(v);
                break;
            }
            case 12: {  // long array
                int32_t n = read<int32_t>();
                std::vector<int64_t> v;
                v.reserve(n);
                for (int32_t i = 0; i < n; ++i) v.push_back(read<int64_t>());
                t.value = std::move(v);
                break;
            }
            default: throw std::runtime_error("NBT: type inconnu " + std::to_string(type));
        }
        return t;
    }

private:
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
};

}  // namespace

Tag parse(const std::vector<uint8_t>& data, bool littleEndian, std::string* rootName) {
    Reader r(data, littleEndian);
    uint8_t type = r.read<uint8_t>();
    if (type != 10) throw std::runtime_error("NBT: la racine n'est pas un compound");
    std::string name = r.readString();
    if (rootName) *rootName = name;
    return r.readPayload(10, 0);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Impossible d'ouvrir " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace baritone::nbt
