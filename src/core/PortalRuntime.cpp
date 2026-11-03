#include "core/Bot.h"
#include "core/Chat.h"
#include "platform/WorldTracker.h"
#include "baritone/world/BlockStateInterface.h"
#include <cmath>
void Bot::startPortalJourney(const std::string& destination) {
    baritone::PortalJourney prepared;
    prepared.start(portals_, identity(), baritone_->playerFeet(), destination);
    cancelAll(); portalJourney_ = std::move(prepared);
    chat::info("Portal journey started; registered entries must point inside active portals");
}
void Bot::tickPortalJourney() {
    if (!portalJourney_.active() || baritone_->pauser().paused()) return;
    auto world = platform::WorldTracker::get().snapshot();
    if (!world) return;
    auto p = player_.position(); baritone::BlockPos feet{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    baritone::BlockStateInterface blocks(world); bool portal = false;
    for (int y = 0; y <= 1; ++y) {
        const auto& name = blocks.props(feet.x, feet.y + y, feet.z).name;
        portal |= name == "minecraft:portal" || name == "minecraft:end_portal" || name == "minecraft:end_gateway";
    }
    const auto goal = portalJourney_.step({worldName_, world->dimension()}, feet, portal);
    if (goal) {
        auto next = std::make_shared<baritone::GoalBlock>(*goal);
        auto current = baritone_->pathing().goal();
        if (!current || !current->sameAs(*next)) baritone_->customGoal().setGoalAndPath(std::move(next));
    } else {
        baritone_->customGoal().onLostControl(); baritone_->pathing().cancelEverything();
        if (!portalJourney_.active()) chat::info(portalJourney_.failure().empty() ? "Portal journey complete" : portalJourney_.failure());
    }
}
