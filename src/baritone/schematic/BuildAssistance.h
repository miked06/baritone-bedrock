#pragma once
#include "baritone/schematic/SchematicWorkspace.h"
namespace baritone {
struct BuildHint { BlockPos position; DesiredBlock block; std::string item, guidance; };
std::optional<BuildHint> nextBuildHint(const SchematicWorkspace& placement,const WorldSnapshot& world,BlockPos player,int radius=8);
struct TechnicalIssue { BlockPos position; std::string block,reason; };
struct TechnicalAudit {
    uint64_t cells=0,technical=0,stateChecks=0;
    std::vector<TechnicalIssue> issues;
    bool truncated=false;
};
// Static placement audit, not a redstone circuit simulator. Non-orientation
// states are not considered automatically reproducible by the current builder.
TechnicalAudit auditTechnicalPlacement(const Schematic& source,BlockPos origin);
}
