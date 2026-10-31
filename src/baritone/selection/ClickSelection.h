#pragma once
#include <optional>
#include "baritone/selection/SelectionManager.h"

namespace baritone {
// Java GuiClick interactions. Bedrock aims with its normal crosshair instead of
// opening a Java screen: left click = goal, right click = above, left drag = box.
class ClickSelection {
public:
    struct Result {
        enum Kind { Goal, Box, Miss } kind;
        BlockPos first{}, second{};
    };
    void start() { active_ = true; button_ = 0; from_.reset(); }
    void cancel() { active_ = false; button_ = 0; from_.reset(); }
    bool active() const { return active_; }
    std::optional<BlockPos> dragStart() const { return button_ == 1 ? from_ : std::nullopt; }
    std::optional<Result> event(int button, bool pressed, std::optional<BlockPos> target) {
        if (!active_ || (button != 1 && button != 2)) return std::nullopt;
        if (pressed) {
            button_ = button;
            from_ = target;
            return std::nullopt;
        }
        if (button_ != button) return std::nullopt;
        const auto from = from_;
        cancel();
        if (!target) return Result{Result::Miss};
        if (button == 1 && from && *from != *target) return Result{Result::Box, *from, *target};
        return Result{Result::Goal, button == 2 ? target->above() : *target};
    }
private:
    bool active_ = false;
    int button_ = 0;
    std::optional<BlockPos> from_;
};
}
