#pragma once
#include <map>
#include <unordered_set>
#include "baritone/schematic/SchematicWorkspace.h"
#include "baritone/api/PlayerContext.h"

namespace baritone {
// Bounded, incremental comparison against exact loaded chunks, never cached terrain.
class SchematicSurvey {
public:
    struct Result {
        uint64_t matching = 0, missing = 0, wrong = 0, extra = 0, unknown = 0, ignored = 0;
        std::map<std::string, uint64_t> items;
        std::vector<SchematicWorkspace::PreviewCell> problems;
        std::vector<std::string> warnings;
    };
    void start(SchematicPtr schematic, BlockPos origin, uint64_t revision);
    bool step(const WorldSnapshot& world, std::size_t budget = 2048);
    void clear() { source_.reset(); result_ = {}; assemblies_.clear(); cursor_ = volume_ = 0; }
    bool active() const { return source_ && cursor_ < volume_; }
    bool ready() const { return source_ && cursor_ == volume_; }
    uint64_t revision() const { return revision_; }
    std::size_t scanned() const { return cursor_; }
    std::size_t volume() const { return volume_; }
    const Result& result() const { return result_; }
private:
    void need(const DesiredBlock& desired, BlockPos local);
    std::unordered_set<BlockPos> assemblies_;
    SchematicPtr source_;
    BlockPos origin_{};
    uint64_t revision_ = 0;
    std::size_t cursor_ = 0, volume_ = 0;
    Result result_;
};
struct MaterialRequirement {
    std::string item;
    uint64_t required = 0, available = 0, missing = 0;
    std::map<std::string, uint64_t> allocated;
};
// Exact inventory items are reserved before substitutes, avoiding double allocation.
std::vector<MaterialRequirement> planMaterials(const std::map<std::string, uint64_t>& requirements,
    const PlayerContext& player, const std::vector<std::string>& substitutions = {});
}
