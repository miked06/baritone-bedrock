#include "baritone/schematic/ConstructionPlan.h"
#include <algorithm>
#include <chrono>
namespace baritone {
namespace {
bool gravity(std::string_view name) { return name == "minecraft:sand" || name == "minecraft:red_sand" || name == "minecraft:gravel" || name.ends_with("concrete_powder") || name.find("anvil") != name.npos; }
class ConstructionPhase : public Schematic {
public:
    ConstructionPhase(SchematicPtr source, BlockPos size, int offset, std::map<BlockPos, DesiredBlock> supports, BlockPos origin, int phase)
        : source_(std::move(source)), size_(size), offset_(offset), supports_(std::move(supports)), origin_(origin), phase_(phase) {}
    BlockPos size() const override { return size_; }
    bool restrictOutside() const override { return true; }
    bool mayPlace(int x, int y, int z) const override { return desired(x,y,z,BlockProps{}).kind == DesiredBlock::Kind::Block; }
    bool mayBreak(int x, int y, int z) const override {
        if (supports_.contains(origin_.offset(x, y, z))) return phase_ == 2;
        return phase_ == 1 && y >= offset_ && source_->inBounds(x, y - offset_, z) && source_->mayBreak(x, y - offset_, z);
    }
    DesiredBlock desired(int x, int y, int z, const BlockProps& current) const override {
        const auto support = supports_.find(origin_.offset(x, y, z));
        if (support != supports_.end()) {
            if (phase_ != 2) return support->second;
            return current.name == support->second.name ? DesiredBlock::air() : DesiredBlock::any();
        }
        return phase_ == 1 && source_->inBounds(x, y - offset_, z) ? source_->desired(x, y - offset_, z, current) : DesiredBlock::any();
    }
private:
    SchematicPtr source_; BlockPos size_, origin_; int offset_, phase_; std::map<BlockPos, DesiredBlock> supports_;
};
}
void ConstructionPlan::start(SchematicPtr source, BlockPos origin, std::string support, int maxDepth, bool permanent) {
    if (!source || schematicVolume(source->size()) > 262144 || maxDepth < 1 || maxDepth > 16 || origin.y < INT32_MIN + maxDepth)
        throw std::invalid_argument("Construction planning: at most 262144 cells, support depth 1..16");
    // Plain, solid supports only: no fluids, falling blocks or block entities.
    if (support != "minecraft:cobblestone" && support != "minecraft:stone" && support != "minecraft:dirt") throw std::invalid_argument("Support material must be cobblestone, stone or dirt");
    source_ = std::move(source); origin_ = origin; size_ = source_->size(); depth_ = maxDepth; permanent_ = permanent; support_ = std::move(support);
    volume_ = schematicVolume(size_); scanned_ = 0; collected_ = ready_ = false; failure_.clear(); final_.clear(); supports_.clear(); dependencies_.clear(); phaseOrigin_ = origin_;
}
bool ConstructionPlan::step(const WorldSnapshot& world, std::size_t budget) {
    if (!source_ || ready_ || !failure_.empty()) return true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    auto blockAt = [&](BlockPos p) -> const BlockProps& {
        auto chunk = world.chunk(p.x >> 4, p.z >> 4);
        if (!chunk || p.y < world.minY() || p.y >= world.maxY()) throw std::invalid_argument("Load the structure, support columns and their ground before planning");
        return BlockRegistry::get().props(chunk->get(p.x & 15, p.y, p.z & 15));
    };
    try {
        for (std::size_t n = 0; n < std::min<std::size_t>(budget, 2048); ++n) {
            if ((n & 31) == 0 && std::chrono::steady_clock::now() >= deadline) break;
            if (!collected_) {
                if (scanned_ == volume_) { collected_ = true; cursor_ = final_.begin(); continue; }
                auto i = scanned_++; const int z = int(i % size_.z), y = int((i / size_.z) % size_.y), x = int(i / (std::size_t(size_.y) * size_.z));
                auto pos = origin_.offset(x, y, z); const auto& current = blockAt(pos); const auto desired = source_->desired(x, y, z, current);
                if (desired.kind == DesiredBlock::Kind::Any) continue;
                if (auto policy = world.protection(); policy && !policy->permitsModify(pos)) throw std::invalid_argument("Construction plan intersects a protected region");
                if (current.hasFluid() || current.unbreakable) throw std::invalid_argument("Contain fluids and remove unsupported obstacles before construction planning");
                final_[pos] = desired; continue;
            }
            if (cursor_ == final_.end()) {
                ready_ = true; size_.y += origin_.y - phaseOrigin_.y; (void)schematicVolume(size_); return true;
            }
            const auto [pos, desired] = *cursor_++;
            if (desired.kind != DesiredBlock::Kind::Block) continue;
            if (blockAt(pos).name == desired.name) continue;
            // Every new block receives a supported chain underneath. The builder
            // still decides the actual face/orientation and confirms world state.
            BlockPos p = pos.below(); bool grounded = false;
            for (int down = 1; down <= depth_; ++down, p = p.below()) {
                const auto& below = blockAt(p);
                if (below.canPlaceAgainst && !below.falling && !below.hasFluid()) { grounded = true; break; }
                if (auto final = final_.find(p); final != final_.end() && final->second.kind == DesiredBlock::Kind::Block) { dependencies_.emplace_back(pos, p); grounded = true; break; }
                if (supports_.contains(p)) { dependencies_.emplace_back(pos, p); grounded = true; break; }
                if (below.type != BlockType::Air || below.hasFluid()) throw std::invalid_argument("A support column would replace an existing block or fluid");
                if (gravity(desired.name) && !permanent_) throw std::invalid_argument("Falling target blocks require permanent foundations");
                if (auto policy = world.protection(); policy && (!policy->permitsModify(p) || !policy->permitsEntry(p))) throw std::invalid_argument("A support column intersects a protected region");
                if (supports_.size() >= 65536) throw std::invalid_argument("Support limit reached (65536 blocks)");
                supports_[p] = DesiredBlock::block(support_); dependencies_.emplace_back(down == 1 ? pos : p.above(), p); phaseOrigin_.y = std::min(phaseOrigin_.y, p.y);
            }
            if (!grounded) throw std::invalid_argument("No stable support found within the chosen depth; adjust the placement or prepare a foundation");
        }
    } catch (const std::exception& e) { failure_ = e.what(); return true; }
    return false;
}
SchematicPtr ConstructionPlan::phase(int phase) const {
    if (!ready_ || phase < 0 || phase > 2) throw std::invalid_argument("Construction plan is not ready");
    return std::make_shared<ConstructionPhase>(source_, size_, origin_.y - phaseOrigin_.y, phase == 2 && permanent_ ? std::map<BlockPos, DesiredBlock>{} : supports_, phaseOrigin_, phase);
}
}
