#include "core/Bot.h"
#include "platform/WorldTracker.h"
void Bot::recordIntent(baritone::BlockPos pos) {
    if (!baritone_ || baritone_->undo().isActive() || worldName_.empty() || !player_.valid()) return;
    auto& tracker = platform::WorldTracker::get(); auto world = tracker.snapshot(); if (!world) return;
    auto& history = baritone_->history(); history.bind({worldName_, world->dimension()});
    player_.refreshWorld(pos,0); auto chunk = world->chunk(pos.x>>4,pos.z>>4);
    if (chunk && pos.y >= world->minY() && pos.y < world->maxY()) history.intent(pos,chunk->get(pos.x&15,pos.y,pos.z&15),ticks_);
}
void Bot::tickHistory() {
    auto world = platform::WorldTracker::get().snapshot(); if (!world) return;
    auto& history = baritone_->history(); history.bind({worldName_,world->dimension()});
    history.observe(ticks_,[&](baritone::BlockPos pos) -> std::optional<baritone::StateId> {
        auto chunk = world->chunk(pos.x>>4,pos.z>>4); if (!chunk || pos.y < world->minY() || pos.y >= world->maxY()) return {};
        player_.refreshWorld(pos,0); return chunk->get(pos.x&15,pos.y,pos.z&15);
    });
}
