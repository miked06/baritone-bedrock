#include "baritone/schematic/Inflate.h"

#include <array>
#include <stdexcept>

namespace baritone {

namespace {

constexpr int kMaxBits = 15;

[[noreturn]] void corrupt(const char* what) {
    throw std::runtime_error(std::string("donnees compressees invalides : ") + what);
}

class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    int bits(int n) {  // LSB first
        int v = bitBuf_;
        while (bitCount_ < n) {
            if (pos_ >= size_) corrupt("fin inattendue");
            v |= static_cast<int>(data_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuf_ = v >> n;
        bitCount_ -= n;
        return v & ((1 << n) - 1);
    }
    void alignToByte() {
        bitBuf_ = 0;
        bitCount_ = 0;
    }
    uint8_t byte() {
        if (pos_ >= size_) corrupt("fin inattendue");
        return data_[pos_++];
    }
    std::size_t position() const { return pos_; }

private:
    const uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    int bitBuf_ = 0;
    int bitCount_ = 0;
};

// Canonical Huffman code: number of codes per length and symbols sorted by code.
struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{};
    std::vector<uint16_t> symbol;

    // Returns > 0 for an incomplete code, < 0 for an over-subscribed one.
    int build(const uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) count[lengths[i]]++;
        symbol.assign(n, 0);
        if (count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return left;
        }
        std::array<uint16_t, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
        for (int sym = 0; sym < n; ++sym)
            if (lengths[sym]) symbol[offs[lengths[sym]]++] = static_cast<uint16_t>(sym);
        return left;
    }

    int decode(BitReader& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= in.bits(1);
            int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        corrupt("code de Huffman");
    }
};

constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void inflateBlock(BitReader& in, std::vector<uint8_t>& out, const Huffman& lencode, const Huffman& distcode) {
    for (;;) {
        int sym = lencode.decode(in);
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) corrupt("longueur");
            int len = kLenBase[sym] + in.bits(kLenExtra[sym]);
            int dsym = distcode.decode(in);
            if (dsym >= 30) corrupt("distance");
            std::size_t dist = kDistBase[dsym] + static_cast<std::size_t>(in.bits(kDistExtra[dsym]));
            if (dist > out.size()) corrupt("distance trop loin");
            std::size_t from = out.size() - dist;
            for (int i = 0; i < len; ++i) out.push_back(out[from + i]);
        }
    }
}

const std::pair<Huffman, Huffman>& fixedCodes() {
    static const std::pair<Huffman, Huffman> codes = [] {
        std::pair<Huffman, Huffman> c;
        uint8_t lengths[288];
        int i = 0;
        for (; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        c.first.build(lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        c.second.build(lengths, 30);
        return c;
    }();
    return codes;
}

void dynamicCodes(BitReader& in, Huffman& lencode, Huffman& distcode) {
    static constexpr uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = in.bits(5) + 257, ndist = in.bits(5) + 1, ncode = in.bits(4) + 4;
    if (nlen > 286 || ndist > 30) corrupt("nombre de codes");
    uint8_t lengths[320] = {};
    for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<uint8_t>(in.bits(3));
    Huffman codecode;
    if (codecode.build(lengths, 19) != 0) corrupt("code des longueurs");
    int index = 0;
    while (index < nlen + ndist) {
        int sym = codecode.decode(in);
        if (sym < 16) {
            lengths[index++] = static_cast<uint8_t>(sym);
            continue;
        }
        uint8_t value = 0;
        int repeat;
        if (sym == 16) {
            if (index == 0) corrupt("repetition sans valeur");
            value = lengths[index - 1];
            repeat = 3 + in.bits(2);
        } else if (sym == 17) {
            repeat = 3 + in.bits(3);
        } else {
            repeat = 11 + in.bits(7);
        }
        if (index + repeat > nlen + ndist) corrupt("trop de longueurs");
        while (repeat--) lengths[index++] = value;
    }
    if (lengths[256] == 0) corrupt("pas de fin de bloc");
    int err = lencode.build(lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) corrupt("code des litteraux");
    err = distcode.build(lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) corrupt("code des distances");
}

}  // namespace

std::vector<uint8_t> inflateRaw(const uint8_t* data, std::size_t size, std::size_t* consumed) {
    BitReader in(data, size);
    std::vector<uint8_t> out;
    out.reserve(size * 4);
    bool last;
    do {
        last = in.bits(1) != 0;
        int type = in.bits(2);
        if (type == 0) {  // stored
            in.alignToByte();
            int len = in.byte() | (in.byte() << 8);
            int nlen = in.byte() | (in.byte() << 8);
            if (len != (~nlen & 0xFFFF)) corrupt("bloc non compresse");
            for (int i = 0; i < len; ++i) out.push_back(in.byte());
        } else if (type == 1) {
            const auto& [lencode, distcode] = fixedCodes();
            inflateBlock(in, out, lencode, distcode);
        } else if (type == 2) {
            Huffman lencode, distcode;
            dynamicCodes(in, lencode, distcode);
            inflateBlock(in, out, lencode, distcode);
        } else {
            corrupt("type de bloc");
        }
    } while (!last);
    if (consumed) *consumed = in.position();
    return out;
}

std::vector<uint8_t> decompressIfNeeded(const std::vector<uint8_t>& data) {
    if (data.size() >= 18 && data[0] == 0x1F && data[1] == 0x8B) {  // gzip
        if (data[2] != 8) corrupt("methode gzip");
        uint8_t flags = data[3];
        std::size_t pos = 10;
        auto need = [&](std::size_t n) {
            if (pos + n > data.size()) corrupt("en-tete gzip");
        };
        if (flags & 4) {  // FEXTRA
            need(2);
            pos += 2 + (data[pos] | (data[pos + 1] << 8));
        }
        if (flags & 8)  // FNAME
            while (need(1), data[pos++] != 0) {}
        if (flags & 16)  // FCOMMENT
            while (need(1), data[pos++] != 0) {}
        if (flags & 2) pos += 2;  // FHCRC
        need(0);
        return inflateRaw(data.data() + pos, data.size() - pos);
    }
    if (data.size() >= 2 && (data[0] & 0x0F) == 8 && ((data[0] << 8) | data[1]) % 31 == 0) {  // zlib
        return inflateRaw(data.data() + 2, data.size() - 2);
    }
    return data;
}

}  // namespace baritone
