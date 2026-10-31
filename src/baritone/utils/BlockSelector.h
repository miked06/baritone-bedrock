#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "baritone/world/BlockProps.h"

namespace baritone {
// Java BlockOptionalMeta syntax, using Bedrock state names and values.
// Omitted properties accept every value: oak_log[pillar_axis=y].
struct BlockSelector {
    std::string name;
    std::vector<std::pair<std::string, std::string>> states;
    static BlockSelector parse(std::string_view text);
    bool matches(const BlockProps& block) const;
    std::string toString() const;
};
}
