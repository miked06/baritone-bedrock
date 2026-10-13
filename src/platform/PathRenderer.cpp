#include "platform/PathRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

#include "baritone/Settings.h"
#include "sdk/Render.h"
#include "platform/WorldTracker.h"

namespace platform {

using baritone::BlockPos;
using baritone::Settings;
using baritone::Vec3d;

namespace {

struct Line {
    Vec3d a, b;
    sdk::Color color;
};

// Java IRenderer.startLines: every line is drawn with alpha 0.4 unless said otherwise
constexpr float kLineAlpha = 0.4f;

sdk::Color color(const baritone::Color& c, float alpha = kLineAlpha) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

constexpr baritone::Color kBuildColor{255, 255, 255};   // ours: the schematic being built
constexpr baritone::Color kElytraColor{255, 255, 0};    // ours: the elytra path

void addBox(std::vector<Line>& out, Vec3d lo, Vec3d hi, sdk::Color c) {
    const Vec3d v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
                        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) out.push_back({v[e[0]], v[e[1]], c});
}

// Java emitAABB(box, expand)
void addBlockBox(std::vector<Line>& out, const BlockPos& p, sdk::Color c, double expand = 0.002) {
    addBox(out, {p.x - expand, p.y - expand, p.z - expand}, {p.x + 1 + expand, p.y + 1 + expand, p.z + 1 + expand}, c);
}

// Box around every block between two corners (inclusive), Java SELECTION_BOX_EXPANSION.
void addRegion(std::vector<Line>& out, const BlockPos& a, const BlockPos& b, sdk::Color c) {
    constexpr double e = 0.005;
    addBox(out, {std::min(a.x, b.x) - e, std::min(a.y, b.y) - e, std::min(a.z, b.z) - e},
           {std::max(a.x, b.x) + 1 + e, std::max(a.y, b.y) + 1 + e, std::max(a.z, b.z) + 1 + e}, c);
}

// Java emitPathLine: the line through the block centers, and with renderPathAsLine off the
// "thingy" (a second line 0.03 higher, closed at both ends).
void emitPathLine(std::vector<Line>& out, const BlockPos& s, const BlockPos& e, sdk::Color c) {
    constexpr double offset = 0.5, extra = offset + 0.03;
    Vec3d a{s.x + offset, s.y + offset, s.z + offset}, b{e.x + offset, e.y + offset, e.z + offset};
    out.push_back({a, b, c});
    if (!Settings::get().renderPathAsLine) {
        Vec3d a2{a.x, s.y + extra, a.z}, b2{b.x, e.y + extra, b.z};
        out.push_back({b, b2, c});
        out.push_back({b2, a2, c});
        out.push_back({a2, a, c});
    }
}

// Java drawPath: straight runs merged, fadePath fades out from fadeStart to fadeEnd
// movements after startIndex.
void drawPath(std::vector<Line>& out, const std::vector<BlockPos>& positions, std::size_t startIndex,
              const baritone::Color& col, bool fadeOut, int fadeStart0 = 10, int fadeEnd0 = 20) {
    const int fadeStart = fadeStart0 + static_cast<int>(startIndex);
    const int fadeEnd = fadeEnd0 + static_cast<int>(startIndex);
    for (std::size_t i = startIndex, next; i + 1 < positions.size(); i = next) {
        const BlockPos& start = positions[i];
        BlockPos end = positions[next = i + 1];
        int dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
        while (next + 1 < positions.size() && (!fadeOut || static_cast<int>(next) + 1 < fadeStart) &&
               dx == positions[next + 1].x - end.x && dy == positions[next + 1].y - end.y &&
               dz == positions[next + 1].z - end.z) {
            end = positions[++next];
        }
        float alpha = kLineAlpha;
        if (fadeOut && static_cast<int>(i) > fadeStart) {
            if (static_cast<int>(i) > fadeEnd) break;
            alpha = kLineAlpha * (1.0f - static_cast<float>(static_cast<int>(i) - fadeStart) / static_cast<float>(fadeEnd - fadeStart));
        }
        emitPathLine(out, start, end, color(col, alpha));
    }
}

// Java drawDankLitGoalBox: two horizontal squares at y1 / y2 (skipped at 0) and the four
// vertical edges from minY to maxY.
void dankLitGoalBox(std::vector<Line>& out, sdk::Color c, double minX, double maxX, double minZ, double maxZ,
                    double minY, double maxY, std::optional<double> y1, std::optional<double> y2) {
    for (const auto& y : {y1, y2}) {
        if (!y) continue;
        out.push_back({{minX, *y, minZ}, {maxX, *y, minZ}, c});
        out.push_back({{maxX, *y, minZ}, {maxX, *y, maxZ}, c});
        out.push_back({{maxX, *y, maxZ}, {minX, *y, maxZ}, c});
        out.push_back({{minX, *y, maxZ}, {minX, *y, minZ}, c});
    }
    for (double y = minY; y < maxY; y += 16) {
        double top = std::min(maxY, y + 16);
        out.push_back({{minX, y, minZ}, {minX, top, minZ}, c});
        out.push_back({{maxX, y, minZ}, {maxX, top, minZ}, c});
        out.push_back({{maxX, y, maxZ}, {maxX, top, maxZ}, c});
        out.push_back({{minX, y, maxZ}, {minX, top, maxZ}, c});
    }
}

// Java drawGoal.
void drawGoal(std::vector<Line>& out, const baritone::Goal* goal, const baritone::Color& col,
              const std::optional<Vec3d>& player, int depth, int& budget) {
    using namespace baritone;
    if (!goal || depth > 4 || budget <= 0) return;
    const Settings& s = Settings::get();
    double y;
    if (!s.renderGoalAnimated) {
        y = 0.999;
    } else {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        y = std::cos(static_cast<double>((ns / 100000LL) % 20000LL) / 20000.0 * std::numbers::pi * 2);
    }
    const sdk::Color c = color(col);

    std::optional<BlockPos> renderPos;  // Java IGoalRenderPos
    bool half = false;                  // GoalGetToBlock / GoalTwoBlocks
    if (auto g1 = dynamic_cast<const GoalGetToBlock*>(goal)) renderPos = g1->pos(), half = true;
    else if (auto g2 = dynamic_cast<const GoalTwoBlocks*>(goal)) renderPos = g2->pos(), half = true;
    else if (auto g3 = dynamic_cast<const GoalBlock*>(goal)) renderPos = g3->pos();
    else if (auto g4 = dynamic_cast<const GoalThreeBlocks*>(goal)) renderPos = g4->pos();
    else if (auto g5 = dynamic_cast<const GoalNear*>(goal)) renderPos = g5->pos();

    if (renderPos) {
        --budget;
        const BlockPos& p = *renderPos;
        double minX = p.x + 0.002, maxX = p.x + 1 - 0.002, minZ = p.z + 0.002, maxZ = p.z + 1 - 0.002;
        if (half) y /= 2;
        double y1 = 1 + y + p.y, y2 = 1 - y + p.y;
        double minY = p.y, maxY = minY + 2;
        if (half) {
            y1 -= 0.5;
            y2 -= 0.5;
            maxY--;
        }
        dankLitGoalBox(out, c, minX, maxX, minZ, maxZ, minY, maxY, y1, y2);
    } else if (auto g = dynamic_cast<const GoalXZ*>(goal)) {
        --budget;
        auto snap = WorldTracker::get().snapshot();
        double minY = snap ? snap->minY() : -64, maxY = snap ? snap->maxY() : 320;
        double minX = g->x() + 0.002, maxX = g->x() + 1 - 0.002, minZ = g->z() + 0.002, maxZ = g->z() + 1 - 0.002;
        dankLitGoalBox(out, c, minX, maxX, minZ, maxZ, minY, maxY, std::nullopt, std::nullopt);
        if (s.renderGoalXZBeacon) {
            // Java draws a beacon beam; here a thinner column inside the box, brighter
            const sdk::Color beam = color(col, 0.8f);
            double cx = g->x() + 0.5, cz = g->z() + 0.5, r = 0.2;
            dankLitGoalBox(out, beam, cx - r, cx + r, cz - r, cz + r, minY, maxY, std::nullopt, std::nullopt);
        }
    } else if (auto gc = dynamic_cast<const GoalComposite*>(goal)) {
        for (const GoalPtr& sub : gc->goals()) drawGoal(out, sub.get(), col, player, depth + 1, budget);
    } else if (auto gi = dynamic_cast<const GoalInverted*>(goal)) {
        drawGoal(out, gi->origin().get(), s.colorInvertedGoalBox, player, depth + 1, budget);
    } else if (auto gy = dynamic_cast<const GoalYLevel*>(goal)) {
        if (!player) return;
        --budget;
        double size = s.yLevelBoxSize;
        double minY = gy->level(), maxY = minY + 2;
        dankLitGoalBox(out, c, player->x - size, player->x + size, player->z - size, player->z + size, minY, maxY,
                       1 + y + gy->level(), 1 - y + gy->level());
    }
}

}  // namespace

PathRenderer& PathRenderer::get() {
    static PathRenderer instance;
    return instance;
}

void PathRenderer::update(Frame frame) {
    std::lock_guard lock(mutex_);
    frame_ = std::move(frame);
}

void PathRenderer::render(sdk::LevelRenderer* renderer, sdk::ScreenContext* screen) {
    const Settings& s = Settings::get();
    std::vector<Line> lines;
    {
        std::lock_guard lock(mutex_);
        const Frame& f = frame_;
        // Java PathRenderer.render
        if (s.renderGoal && f.goal) {
            int budget = 100;
            drawGoal(lines, f.goal.get(), s.colorGoalBox, f.player, 0, budget);
        }
        if (s.renderPath) {
            if (s.renderSelectionBoxes) {
                for (const BlockPos& p : f.toBreak) addBlockBox(lines, p, color(s.colorBlocksToBreak));
                for (const BlockPos& p : f.toPlace) addBlockBox(lines, p, color(s.colorBlocksToPlace));
                for (const BlockPos& p : f.toWalkInto) addBlockBox(lines, p, color(s.colorBlocksToWalkInto));
                // ours: the block being mined right now
                if (f.breaking) addBlockBox(lines, *f.breaking, color(s.colorBlocksToBreak, 0.8f), 0.004);
            }
            if (!f.path.empty()) {
                std::size_t begin = f.pathPosition >= 3 ? f.pathPosition - 3 : 0;
                drawPath(lines, f.path, begin, s.colorCurrentPath, s.fadePath);
            }
            drawPath(lines, f.next, 0, s.colorNextPath, s.fadePath);
            drawPath(lines, f.calculating, 0, s.colorBestPathSoFar, s.fadePath);
            if (!f.mostRecent.empty()) {
                drawPath(lines, f.mostRecent, 0, s.colorMostRecentConsidered, s.fadePath);
                addBlockBox(lines, f.mostRecent.back(), color(s.colorMostRecentConsidered));
            }
        }
        // Java ElytraBehavior.onRenderPass
        if (s.renderElytraPath || s.elytraRenderSimulation) {
            const sdk::Color ec = color(kElytraColor);
            if (s.renderElytraPath) {
                for (std::size_t i = 1; i < f.elytraPath.size(); ++i) {
                    const BlockPos& a = f.elytraPath[i - 1];
                    const BlockPos& b = f.elytraPath[i];
                    lines.push_back({{a.x + 0.5, a.y + 0.5, a.z + 0.5}, {b.x + 0.5, b.y + 0.5, b.z + 0.5}, ec});
                }
                if (f.elytraAim) {  // a small cross where it steers
                    const Vec3d& a = *f.elytraAim;
                    lines.push_back({{a.x - 1, a.y, a.z}, {a.x + 1, a.y, a.z}, ec});
                    lines.push_back({{a.x, a.y - 1, a.z}, {a.x, a.y + 1, a.z}, ec});
                    lines.push_back({{a.x, a.y, a.z - 1}, {a.x, a.y, a.z + 1}, ec});
                }
                if (f.elytraLanding) {  // the landing spot, with a beam to find it from far away
                    const BlockPos& l = *f.elytraLanding;
                    const sdk::Color gc = color(s.colorGoalBox);
                    addBlockBox(lines, l, gc);
                    lines.push_back({{l.x + 0.5, l.y + 1.0, l.z + 0.5}, {l.x + 0.5, l.y + 256.0, l.z + 0.5}, gc});
                }
            }
            // elytraRenderSimulation: the simulated flight of the chosen pitch (Java: white)
            const sdk::Color sim{1, 1, 1, kLineAlpha};
            for (std::size_t i = 1; i < f.elytraSimulation.size(); ++i)
                lines.push_back({f.elytraSimulation[i - 1], f.elytraSimulation[i], sim});
        }
        // Java SelectionRenderer
        if (s.renderSelection) {
            const float opacity = static_cast<float>(std::clamp(s.selectionOpacity, 0.0, 1.0));
            for (const auto& [a, b] : f.selections) addRegion(lines, a, b, color(s.colorSelection, opacity));
            if (s.renderSelectionCorners) {
                for (const auto& [a, b] : f.selections) {
                    addBlockBox(lines, a, color(s.colorSelectionPos1, opacity), 0);
                    addBlockBox(lines, b, color(s.colorSelectionPos2, opacity), 0);
                }
                if (f.selPos1) addBlockBox(lines, *f.selPos1, color(s.colorSelectionPos1, opacity), 0);
            }
            if (f.build) addRegion(lines, f.build->first, f.build->second, color(kBuildColor, opacity));
        }
    }
    if (lines.empty() || !renderer || !screen) return;

    sdk::Tessellator* tess = screen->tessellator();
    sdk::MaterialPtr* mat = sdk::material("selection_box");
    sdk::Vec3 origin = renderer->origin();
    if (!tess || !mat) return;
    if (sdk::Color* shader = screen->shaderColor()) *shader = {1, 1, 1, 1};

    tess->begin(sdk::Primitive::LineList, static_cast<int>(lines.size() * 2));
    for (const Line& l : lines) {
        tess->color(l.color);
        tess->vertex(static_cast<float>(l.a.x - origin.x), static_cast<float>(l.a.y - origin.y),
                     static_cast<float>(l.a.z - origin.z));
        tess->color(l.color);
        tess->vertex(static_cast<float>(l.b.x - origin.x), static_cast<float>(l.b.y - origin.y),
                     static_cast<float>(l.b.z - origin.z));
    }
    sdk::renderMeshImmediately(screen, tess, mat);
}

}  // namespace platform
