#include "baritone/world/PortalJourney.h"
namespace baritone {
void PortalJourney::start(const PortalGraph& graph, WorldIdentity identity, BlockPos pos, const std::string& destination) {
    auto route = graph.route(identity.dimension, pos, destination);
    graph_ = graph; route_ = std::move(route); identity_ = std::move(identity);
    next_ = ticks_ = legTicks_ = cooldown_ = 0; active_ = true; armed_ = false; failure_.clear();
}
std::optional<BlockPos> PortalJourney::step(const WorldIdentity& id, BlockPos pos, bool portalPresent) {
    if (!active_) return {};
    if (!(id == identity_)) { fail("Unexpected world/dimension change; portal journey cancelled"); return {}; }
    if (++ticks_ > 20 * 60 * 60 || ++legTicks_ > 20 * 60 * 3) { fail("Portal journey deadline reached; verify the registered portal and its exit"); return {}; }
    if (cooldown_) { --cooldown_; return {}; }
    if (next_ >= route_.stops.size()) { cancel(); return {}; }
    const auto& node = graph_.nodes().at(route_.stops[next_]);
    if (node.dimension != id.dimension) { fail("The next portal is in an unconfirmed dimension"); return {}; }
    if (node.position.distSq(pos) > 1) return node.position;
    if (next_ + 1 < route_.stops.size() && graph_.nodes().at(route_.stops[next_ + 1]).dimension != id.dimension) {
        if (!portalPresent) { fail("The registered entry is not an active portal at the player's position"); return {}; }
        armed_ = true; return node.position;
    }
    ++next_; legTicks_ = 0;
    if (next_ == route_.stops.size()) { cancel(); return {}; }
    return graph_.nodes().at(route_.stops[next_]).position;
}
bool PortalJourney::transition(const WorldIdentity& id, BlockPos pos) {
    if (!active_ || !armed_ || id.name != identity_.name || next_ + 1 >= route_.stops.size()) { fail("Unconfirmed portal transition; journey cancelled"); return false; }
    const auto& from = route_.stops[next_]; const auto& to = route_.stops[next_ + 1];
    const auto& exit = graph_.nodes().at(to);
    if (!graph_.links().contains({from, to}) || exit.dimension != id.dimension || exit.position.distSq(pos) > 64) {
        fail("Portal exit does not match the registered link (dimension and eight-block arrival radius required)"); return false;
    }
    identity_ = id; next_ += 2; armed_ = false; legTicks_ = 0; cooldown_ = 20;
    if (next_ >= route_.stops.size()) active_ = false;
    return true;
}
}
