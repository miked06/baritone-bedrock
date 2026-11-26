#include "core/Bot.h"

#include <chrono>
#include <format>

#include "baritone/pathing/calc/AStarPathFinder.h"
#include "core/Chat.h"
#include "core/Log.h"
#include "platform/WorldTracker.h"

void Bot::startPathPreview(baritone::BlockPos start, baritone::GoalPtr goal) {
    auto world = platform::WorldTracker::get().snapshot();
    if (!world) throw std::invalid_argument("No world snapshot available");
    pollPathPreview();
    if (pathPreviewJob_.valid()) throw std::invalid_argument("A path preview is still being calculated");
    auto abilities = inventory().abilities();
    auto task = std::make_shared<std::packaged_task<baritone::PathPreview()>>([world, start, goal, abilities] {
        const auto begin = std::chrono::steady_clock::now();
        auto context = std::make_unique<baritone::CalculationContext>(world, abilities, true);
        baritone::CalculationContext inspection(world, abilities, true);
        baritone::AStarPathFinder finder(start, goal, std::move(context), {});
        auto result = finder.calculate(2000, 5000);
        auto preview = result.path ? baritone::describePath(std::move(result.path), inspection) : baritone::PathPreview{};
        if (preview.path) {
            std::string moves;
            constexpr const char* names[]{"T", "A", "D", "F", "Di", "P", "Dw", "Pk", "PkC", "PkS"};
            for (const auto& move : preview.path->movements()) {
                moves += names[static_cast<int>(move.type)];
                moves += ' ';
            }
            logger::info("path: {}", moves);
        }
        preview.complete = result.type == baritone::PathResultType::SuccessToGoal;
        preview.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        preview.error = std::move(result.error);
        return preview;
    });
    runDiagnosticAsync([task] { (*task)(); });
    pathPreviewJob_ = task->get_future();
    pathPreview_.reset();
    pathPreviewWanted_ = true;
    chat::info(std::format("Previewing path from {} to {}", start, goal->toString()));
}

void Bot::clearPathPreview() {
    pathPreviewWanted_ = false;
    pathPreview_.reset();
}

void Bot::pollPathPreview() {
    if (!pathPreviewJob_.valid() || pathPreviewJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        auto preview = pathPreviewJob_.get();
        if (!pathPreviewWanted_) return;
        if (!preview.path) {
            chat::error(std::format("No path found ({:.0f} ms) {}", preview.milliseconds, preview.error));
            return;
        }
        chat::info(std::format("Path {}: {} positions, estimated {:.1f} s, {} nodes, {:.0f} ms",
                               preview.complete ? "complete" : "segment", preview.path->length(),
                               preview.path->totalTicks() / 20.0, preview.path->numNodesConsidered(), preview.milliseconds));
        chat::info(std::format("Preview: {} blocks to break, {} to place, {} nearby hazards, largest drop {} blocks, {} cached moves",
                               preview.toBreak.size(), preview.toPlace.size(), preview.hazards.size(), preview.largestDrop, preview.cachedMoves));
        chat::info("Cyan route; red break boxes, green placement boxes and amber hazard boxes. Use #calc clear to hide it");
        pathPreview_ = std::make_shared<baritone::PathPreview>(std::move(preview));
    } catch (const std::exception& error) {
        if (pathPreviewWanted_) chat::error(std::string("Path preview failed: ") + error.what());
    }
}
