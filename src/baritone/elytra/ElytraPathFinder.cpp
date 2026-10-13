#include "baritone/elytra/ElytraPathFinder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <unordered_map>

#include "baritone/world/BlockStateInterface.h"

namespace baritone::elytra {

namespace {

struct Node {
    BlockPos pos;
    double g;
    int parent;
};

bool solid(BlockStateInterface& bsi, int x, int y, int z, bool useCache) {
    if (!useCache && !bsi.isLoadedLive(x, z)) return false;  // only what is loaded now
    const BlockProps& p = bsi.props(x, y, z);
    return p.collides || p.fluid == Fluid::Lava;
}

double distance(const BlockPos& a, const BlockPos& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

FlightPathResult ElytraPathFinder::calculate(const BlockPos& start, const BlockPos& goal, double goalRadius,
                                             long timeoutMs, int maxNodes) const {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    BlockStateInterface bsi(world_);
    int minY = bsi.minY() + 1, maxY = bsi.maxY() - 2;
    if (world_->dimension() == 1) maxY = bsi.maxY() - 6;  // nether: under the bedrock roof (unknown chunks = air)
    if (maxY_) maxY = *maxY_;

    // free cells (with the clearance around them), memoized: the same cells are tested a lot
    std::unordered_map<BlockPos, bool> freeCache;
    auto isFree = [&](const BlockPos& p) {
        if (p.y < minY || p.y > maxY) return false;
        auto it = freeCache.find(p);
        if (it != freeCache.end()) return it->second;
        bool free = true;
        for (int dx = -clearance_; dx <= clearance_ && free; ++dx)
            for (int dy = -clearance_; dy <= clearance_ && free; ++dy)
                for (int dz = -clearance_; dz <= clearance_ && free; ++dz)
                    if (solid(bsi, p.x + dx, p.y + dy, p.z + dz, useCache_)) free = false;
        freeCache.emplace(p, free);
        return free;
    };
    // a cell right next to obstacles is allowed but costs more, to keep some room
    auto nearPenalty = [&](const BlockPos& p) {
        for (int d = 2; d <= 3; ++d)
            if (solid(bsi, p.x, p.y - d, p.z, useCache_) || solid(bsi, p.x, p.y + d, p.z, useCache_) || solid(bsi, p.x + d, p.y, p.z, useCache_) ||
                solid(bsi, p.x - d, p.y, p.z, useCache_) || solid(bsi, p.x, p.y, p.z + d, useCache_) || solid(bsi, p.x, p.y, p.z - d, useCache_)) {
                return d == 2 ? 2.0 : 0.5;
            }
        return 0.0;
    };

    constexpr double kWeight = 1.3;  // a little greedy: open air makes plain A* expand a lot
    std::vector<Node> nodes;
    std::unordered_map<BlockPos, int> index;
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    nodes.push_back({start, 0, -1});
    index[start] = 0;
    open.push({distance(start, goal) * kWeight, 0});

    int best = 0;
    double bestDist = distance(start, goal);
    FlightPathResult result;
    while (!open.empty()) {
        auto [f, i] = open.top();
        open.pop();
        Node cur = nodes[i];
        if (f - distance(cur.pos, goal) * kWeight > cur.g + 1e-6) continue;  // stale entry
        double d = distance(cur.pos, goal);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
        if (d <= goalRadius) {
            best = i;
            result.reachesGoal = true;
            break;
        }
        if (static_cast<int>(nodes.size()) > maxNodes || cancelled_ || (nodes.size() % 1024 == 0 && Clock::now() > deadline)) break;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (!dx && !dy && !dz) continue;
                    BlockPos n = cur.pos.offset(dx, dy, dz);
                    if (!isFree(n)) continue;
                    double g = cur.g + std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)) + nearPenalty(n);
                    auto it = index.find(n);
                    if (it != index.end() && nodes[it->second].g <= g) continue;
                    int ni;
                    if (it == index.end()) {
                        ni = static_cast<int>(nodes.size());
                        nodes.push_back({n, g, i});
                        index.emplace(n, ni);
                    } else {
                        ni = it->second;
                        nodes[ni].g = g;
                        nodes[ni].parent = i;
                    }
                    open.push({g + distance(n, goal) * kWeight, ni});
                }
    }
    for (int i = best; i >= 0; i = nodes[i].parent) result.path.push_back(nodes[i].pos);
    std::reverse(result.path.begin(), result.path.end());
    result.nodes = static_cast<int>(nodes.size());
    return result;
}

}  // namespace baritone::elytra
