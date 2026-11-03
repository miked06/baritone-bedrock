#include "baritone/schematic/MiningPlan.h"
#include "baritone/behavior/InventoryBehavior.h"
#include <algorithm>
#include <cmath>
#include <chrono>
namespace baritone {
MiningPlan::MiningPlan(Selection area, std::string method, BlockFilter objective) : origin_(area.min()), size_(area.size()), method_(std::move(method)), objective_(std::move(objective)) {
    if (method_ != "vein" && method_ != "tunnel" && method_ != "branch") throw std::invalid_argument("Mining methods: vein, tunnel, branch");
    volume_ = schematicVolume(size_);
    if (volume_ > 262144 || size_.x > 128 || size_.z > 128 || size_.y > 128 || (method_ != "vein" && size_.y < 2)) throw std::invalid_argument("Mining selection: at most 128 per axis and 262144 cells; tunnels/branches need two-block height");
    if (origin_.x <= INT32_MIN + 1 || origin_.y <= INT32_MIN + 1 || origin_.z <= INT32_MIN + 1 || area.max().x >= INT32_MAX - 1 || area.max().y >= INT32_MAX - 1 || area.max().z >= INT32_MAX - 1) throw std::invalid_argument("Mining boundary coordinates overflow");
}
bool MiningPlan::step(const WorldSnapshot& world, const PlayerContext& player, std::size_t budget) {
    if (ready_ || !failure_.empty()) return true;
    auto at = [&](BlockPos p) -> const BlockProps& {
        auto chunk = world.chunk(p.x >> 4, p.z >> 4);
        if (!chunk || p.y < world.minY() || p.y >= world.maxY()) throw std::invalid_argument("Load the complete mining area and adjacent boundary");
        return BlockRegistry::get().props(chunk->get(p.x & 15, p.y, p.z & 15));
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    try {
        for (std::size_t n = 0; n < std::min<std::size_t>(budget,2048) && cursor_ < volume_; ++n) {
            if ((n & 31) == 0 && std::chrono::steady_clock::now() >= deadline) break;
            auto i = cursor_++; const int z = int(i % size_.z), y = int((i / size_.z) % size_.y), x = int(i / (std::size_t(size_.y) * size_.z));
            const auto p = origin_.offset(x,y,z); const auto& block = at(p);
            const bool excavate = method_ == "vein" ? objective_.matches(block) : y < 2 && (z == size_.z / 2 || (method_ == "branch" && x % 4 == 0));
            if (!excavate) continue;
            if (auto policy = world.protection(); policy && (!policy->permitsModify(p) || !policy->permitsEntry(p))) throw std::invalid_argument("Mining plan intersects a protected zone");
            if (block.unbreakable || block.hardness < 0) throw std::invalid_argument("Mining plan contains an unbreakable obstacle");
            if (block.hasFluid()) throw std::invalid_argument("Contain liquids before starting the mining plan");
            for (auto d : {Direction::Down, Direction::Up, Direction::North, Direction::South, Direction::West, Direction::East}) if (at(p.offset(d)).hasFluid()) throw std::invalid_argument("Liquid beside a planned excavation; contain it first");
            if (at(p.above()).falling) throw std::invalid_argument("Falling blocks above excavation; secure the roof first");
            if (method_ != "vein" && y == 0 && !at(p.below()).fullCube) throw std::invalid_argument("Tunnels need a continuous existing solid floor");
            cells_.insert({x,y,z});
            if (block.type == BlockType::Air) continue;
            ++blocks_; if (objective_.matches(block)) ++objectives_;
            double speed = InventoryBehavior::breakSpeed({}, block);
            for (int slot = 0; slot < 9; ++slot) speed = std::max(speed, InventoryBehavior::breakSpeed(player.hotbar(slot), block));
            if (speed <= 0 || !std::isfinite(speed)) ticks_ += speed > 0 ? 1 : 1200;
            else ticks_ += std::ceil(1 / speed);
        }
    } catch (const std::exception& e) { failure_ = e.what(); return true; }
    ready_ = cursor_ == volume_; return ready_;
}
}
