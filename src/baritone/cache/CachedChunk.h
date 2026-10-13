#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/world/BlockProps.h"

namespace baritone {

class ChunkData;

// What the pathfinder remembers of a block it can no longer see (Baritone's PathingBlockType).
enum class PathingBlockType : uint8_t { Air = 0, Water = 1, Avoid = 2, Solid = 3 };

// ChunkPacker.getPathingBlockType
PathingBlockType pathingTypeOf(const BlockProps& p);
// CachedChunk.BLOCKS_TO_KEEP_TRACK_OF (+ the valuable ores): their positions are kept so
// #mine / #goto can find them far away.
bool isTrackedBlock(std::string_view name);

// One chunk column in 2 bits per block, plus the positions of tracked blocks
// (Baritone's CachedChunk). Immutable once built: replaced as a whole when re-packed.
class CachedChunk {
public:
    using Special = std::unordered_map<std::string, std::vector<BlockPos>>;  // name -> absolute positions

    CachedChunk(int cx, int cz, int minY, int height);

    // Packs a snapshot chunk (game thread).
    static std::shared_ptr<CachedChunk> pack(const ChunkData& chunk, int minY, int height, int64_t timestamp);

    int cx() const { return cx_; }
    int cz() const { return cz_; }
    int minY() const { return minY_; }
    int height() const { return height_; }
    int64_t timestamp() const { return timestamp_; }
    void setTimestamp(int64_t t) { timestamp_ = t; }

    // y must be inside [minY, minY + height)
    PathingBlockType type(int lx, int y, int lz) const {
        int i = ((y - minY_) << 8) | (lz << 4) | lx;
        return static_cast<PathingBlockType>((bits_[i >> 2] >> ((i & 3) * 2)) & 3);
    }
    void setType(int lx, int y, int lz, PathingBlockType t) {
        int i = ((y - minY_) << 8) | (lz << 4) | lx;
        int shift = (i & 3) * 2;
        bits_[i >> 2] = static_cast<uint8_t>((bits_[i >> 2] & ~(3 << shift)) | (static_cast<int>(t) << shift));
    }

    std::vector<uint8_t>& bits() { return bits_; }
    const std::vector<uint8_t>& bits() const { return bits_; }
    Special& special() { return special_; }
    const Special& special() const { return special_; }

private:
    int cx_, cz_, minY_, height_;
    int64_t timestamp_ = 0;
    std::vector<uint8_t> bits_;  // index ((y - minY) << 8) | (z << 4) | x, 4 blocks per byte
    Special special_;
};

}  // namespace baritone
