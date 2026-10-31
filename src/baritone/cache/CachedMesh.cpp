#include "baritone/cache/CachedMesh.h"
#include <algorithm>
#include <unordered_map>

namespace baritone {
std::vector<CachedFace> buildCachedMesh(CachedWorld& cache, const WorldSnapshot& loaded,
    const BlockPos& center, int chunkRadius, std::size_t maxFaces) {
    std::vector<CachedFace> faces;
    if (chunkRadius < 0 || maxFaces == 0) return faces;
    chunkRadius = std::min(chunkRadius, 32);
    const int cx = center.x >> 4, cz = center.z >> 4;
    std::vector<std::pair<int, int>> candidates;
    for (int dx = -chunkRadius; dx <= chunkRadius; ++dx)
        for (int dz = -chunkRadius; dz <= chunkRadius; ++dz)
            if (!loaded.isLoaded(cx + dx, cz + dz)) candidates.emplace_back(cx + dx, cz + dz);
    std::ranges::sort(candidates, [&](auto a, auto b) {
        auto dist = [&](auto c) { return (c.first - cx) * (c.first - cx) + (c.second - cz) * (c.second - cz); };
        return dist(a) < dist(b);
    });
    std::unordered_map<int64_t, std::shared_ptr<const CachedChunk>> held;
    auto chunkAt = [&](int x, int z) {
        auto [it, inserted] = held.try_emplace(WorldSnapshot::key(x, z));
        if (inserted) it->second = cache.chunk(x, z);
        return it->second;
    };
    auto typeAt = [&](int x, int y, int z) {
        if (y < cache.minY() || y >= cache.minY() + cache.height()) return PathingBlockType::Air;
        if (auto real = loaded.chunk(x >> 4, z >> 4))
            return pathingTypeOf(BlockRegistry::get().props(real->get(x & 15, y, z & 15)));
        auto chunk = chunkAt(x >> 4, z >> 4);
        return chunk ? chunk->type(x & 15, y, z & 15) : PathingBlockType::Air;
    };
    static constexpr int normals[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    static constexpr int corners[6][4][3] = {
        {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, {{1,0,1},{1,0,0},{1,1,0},{1,1,1}},
        {{0,0,1},{0,0,0},{1,0,0},{1,0,1}}, {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},
        {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}
    };
    faces.reserve(std::min<std::size_t>(maxFaces, 8192));
    for (auto [xChunk, zChunk] : candidates) {
        auto chunk = chunkAt(xChunk, zChunk);
        if (!chunk) continue;
        for (int y = chunk->minY(); y < chunk->minY() + chunk->height(); ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    auto type = chunk->type(x, y, z);
                    if (type == PathingBlockType::Air) continue;
                    const int wx = xChunk * 16 + x, wz = zChunk * 16 + z;
                    for (int side = 0; side < 6; ++side) {
                        auto next = typeAt(wx + normals[side][0], y + normals[side][1], wz + normals[side][2]);
                        if (next == type || next == PathingBlockType::Solid) continue;
                        CachedFace face;
                        face.type = type;
                        for (int v = 0; v < 4; ++v)
                            face.vertices[v] = {double(wx + corners[side][v][0]), double(y + corners[side][v][1]),
                                                double(wz + corners[side][v][2])};
                        faces.push_back(face);
                        if (faces.size() >= maxFaces) return faces;
                    }
                }
    }
    return faces;
}
}
