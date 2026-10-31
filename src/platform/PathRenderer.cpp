#include "platform/PathRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <map>
#include <tuple>

#include "baritone/Settings.h"
#include "sdk/Render.h"
#include "platform/WorldTracker.h"
#include "core/Log.h"
#include "core/Perf.h"

namespace platform {

using baritone::BlockPos;
using baritone::Settings;
using baritone::Vec3d;

namespace {

struct Line {
    Vec3d a, b;
    sdk::Color color;
    float width = 0;  // world units; 0 = a 1-pixel line
    bool ignoreDepth = false;
};

// Java's line widths are in pixels; Bedrock's line primitive is always 1 pixel, so wider
// lines are drawn as two crossed ribbons. About 0.012 block per pixel at ~10 blocks away.
float worldWidth(double pixels) { return pixels <= 1.0 ? 0.0f : static_cast<float>(pixels * 0.012); }
void widen(std::vector<Line>& lines, std::size_t from, double pixels) {
    float w = worldWidth(pixels);
    for (std::size_t i = from; i < lines.size(); ++i) if (lines[i].width == 0) lines[i].width = w;
}

void depthMode(std::vector<Line>& lines, std::size_t from, bool ignore) {
    for (std::size_t i = from; i < lines.size(); ++i) lines[i].ignoreDepth = ignore;
}

// Java IRenderer.startLines: every line is drawn with alpha 0.4 unless said otherwise
constexpr float kLineAlpha = 0.4f;

sdk::Color color(const baritone::Color& c, float alpha = kLineAlpha) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

constexpr baritone::Color kBuildColor{255, 255, 255};   // ours: the schematic being built
constexpr baritone::Color kElytraColor{255, 255, 0};    // ours: the elytra path

sdk::Color schematicTint(baritone::SchematicWorkspace::Difference difference, float alpha) {
    using D = baritone::SchematicWorkspace::Difference;
    if (difference == D::Missing) return {0.1f, 0.9f, 1, alpha};
    if (difference == D::Wrong) return {1, 0.15f, 0.15f, alpha};
    if (difference == D::Remove) return {1, 0.65f, 0.1f, alpha};
    return {0.6f, 0.6f, 0.6f, alpha * 0.5f};
}

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
            // Two translucent crossed surfaces, with a bright core. The game
            // provides no Java beacon texture, but this is a filled beam, not an outline.
            double cx = g->x() + 0.5, cz = g->z() + 0.5;
            out.push_back({{cx, minY, cz}, {cx, maxY, cz}, color(col, 0.25f), 0.4f});
            out.push_back({{cx, minY, cz}, {cx, maxY, cz}, color(col, 0.65f), 0.15f});
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
    // Explicit shutdown joins workers during DLL unload. At process termination
    // Windows may already have killed other threads before static destructors run.
    static PathRenderer* instance = new PathRenderer;
    return *instance;
}

void PathRenderer::update(Frame frame) {
    std::lock_guard updateLock(updateMutex_);
    if (stopping_) return;
    ++meshTicks_;
    auto world = WorldTracker::get().snapshot();
    auto cache = world ? world->cache() : nullptr;
    const bool enabled = Settings::get().renderCachedChunks && frame.player && cache;
    if (meshFuture_.valid() && meshFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto mesh = meshFuture_.get();
            if (enabled && buildingSource_ == cache)
                cachedMesh_ = std::make_shared<const std::vector<baritone::CachedFace>>(std::move(mesh));
        } catch (const std::exception& e) {
            logger::warn("Cached terrain mesh failed: {}", e.what());
        }
        buildingSource_.reset();
    }
    if (!enabled || meshSource_ != cache) {
        cachedMesh_.reset();
        meshSource_ = enabled ? cache : nullptr;
        meshStarted_ = 0;
    }
    if (enabled) {
        if (!meshFuture_.valid() && (meshStarted_ == 0 || meshTicks_ - meshStarted_ >= 20)) {
            const auto& player = *frame.player;
            baritone::BlockPos center{static_cast<int>(std::floor(player.x)), static_cast<int>(std::floor(player.y)),
                                      static_cast<int>(std::floor(player.z))};
            buildingSource_ = cache;
            meshStarted_ = meshTicks_;
            meshFuture_ = std::async(std::launch::async, [cache, world, center] {
                return baritone::buildCachedMesh(*cache, *world, center);
            });
        }
        frame.cachedMesh = cachedMesh_;
    }
    std::lock_guard lock(mutex_);
    frame_ = std::move(frame);
}

void PathRenderer::shutdown() {
    stopping_ = true;
    std::lock_guard updateLock(updateMutex_);
    if (meshFuture_.valid()) {
        try { meshFuture_.get(); }
        catch (const std::exception& e) { logger::warn("Cached terrain mesh shutdown: {}", e.what()); }
    }
    cachedMesh_.reset();
    meshSource_.reset();
    buildingSource_.reset();
    std::lock_guard frameLock(mutex_);
    frame_ = {};
}

void PathRenderer::render(sdk::LevelRenderer* renderer, sdk::ScreenContext* screen) {
    perf::Scope timing(perf::Metric::Overlay);
    const Settings& s = Settings::get();
    std::vector<Line> lines;
    std::shared_ptr<const std::vector<baritone::CachedFace>> cachedMesh;
    std::vector<baritone::SchematicWorkspace::PreviewCell> schematicCells;
    {
        std::lock_guard lock(mutex_);
        const Frame& f = frame_;
        cachedMesh = f.cachedMesh;
        schematicCells = f.schematicPreview;
        // Java PathRenderer.render
        if (s.renderGoal && f.goal) {
            int budget = 100;
            drawGoal(lines, f.goal.get(), s.colorGoalBox, f.player, 0, budget);
            widen(lines, 0, s.goalRenderLineWidthPixels);
            depthMode(lines, 0, s.renderGoalIgnoreDepth);
        }
        const std::size_t pathLines = lines.size();
        if (s.renderPath) {
            if (s.renderSelectionBoxes) {
                for (const BlockPos& p : f.toBreak) addBlockBox(lines, p, color(s.colorBlocksToBreak));
                for (const BlockPos& p : f.toPlace) addBlockBox(lines, p, color(s.colorBlocksToPlace));
                for (const BlockPos& p : f.toWalkInto) addBlockBox(lines, p, color(s.colorBlocksToWalkInto));
                // ours: the block being mined right now
                if (f.breaking) addBlockBox(lines, *f.breaking, color(s.colorBlocksToBreak, 0.8f), 0.004);
                depthMode(lines, pathLines, s.renderSelectionBoxesIgnoreDepth);
            }
            const std::size_t movementLines = lines.size();
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
            depthMode(lines, movementLines, s.renderPathIgnoreDepth);
        }
        widen(lines, pathLines, s.pathRenderLineWidthPixels);
        // Java ElytraBehavior.onRenderPass
        const std::size_t elytraLines = lines.size();
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
        widen(lines, elytraLines, s.pathRenderLineWidthPixels);
        if (s.elytraRenderRaytraces) {
            for (const auto& ray : f.elytraRaytraces)
                lines.push_back({ray.from, ray.to, ray.blocked ? sdk::Color{1, 0.2f, 0.1f, 0.5f} : sdk::Color{0.1f, 1, 0.2f, 0.3f}});
        }
        if (s.elytraRenderHitboxRaytraces) {
            constexpr double half = baritone::elytra::kGlideWidth / 2;
            for (const auto& sample : f.elytraHitboxSamples) {
                const auto& p = sample.position;
                addBox(lines, {p.x - half, p.y, p.z - half},
                    {p.x + half, p.y + baritone::elytra::kGlideHeight, p.z + half},
                    sample.collided ? sdk::Color{1, 0.1f, 0.1f, 0.7f} : sdk::Color{0.2f, 0.8f, 1, 0.15f});
            }
        }
        depthMode(lines, elytraLines, s.renderPathIgnoreDepth);
        // Java SelectionRenderer
        const std::size_t selectionLines = lines.size();
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
        widen(lines, selectionLines, s.selectionLineWidth);
        depthMode(lines, selectionLines, s.renderSelectionIgnoreDepth);
        const std::size_t clickLines = lines.size();
        if (f.clickTarget) addBlockBox(lines, *f.clickTarget, {0, 1, 1, 0.7f});
        if (f.clickTarget && f.clickStart && *f.clickTarget != *f.clickStart)
            addRegion(lines, *f.clickStart, *f.clickTarget, {1, 0.2f, 0.2f, 0.6f});
        widen(lines, clickLines, s.selectionLineWidth);
        depthMode(lines, clickLines, true);
        const std::size_t schematicLines = lines.size();
        if (f.schematicBounds) addRegion(lines, f.schematicBounds->first, f.schematicBounds->second, {0.6f, 0.8f, 1, 0.35f});
        for (const auto& cell : f.schematicPreview) {
            addBlockBox(lines, cell.pos, schematicTint(cell.difference, 0.75f), 0.003);
        }
        // Wire ghosts remain visible through existing terrain. The bounded
        // placement preview has its own on/off switch, independent of path lines.
        widen(lines, schematicLines, 1);
        depthMode(lines, schematicLines, true);
    }
    if ((lines.empty() && !cachedMesh) || !renderer || !screen) return;

    sdk::Tessellator* tess = screen->tessellator();
    sdk::Vec3 origin = renderer->origin();
    sdk::ShaderColor* shader = screen->shaderColor();
    static bool loggedSchematic = false;
    if (!schematicCells.empty() && !loggedSchematic) {
        logger::debug("Schematic render: {} boxes, camera origin {} {} {}, tessellator {}, shader {}",
            schematicCells.size(), origin.x, origin.y, origin.z, static_cast<void*>(tess), static_cast<void*>(shader));
        loggedSchematic = true;
    }
    if (!tess || !shader) return;
    struct RestoreColor {
        sdk::ShaderColor* target;
        sdk::Color saved;
        ~RestoreColor() { target->set(saved); }
    } restore{shader, shader->color};

    auto put = [&](const Vec3d& v, const sdk::Color& c) {
        tess->color(c);
        tess->vertex(static_cast<float>(v.x - origin.x), static_cast<float>(v.y - origin.y), static_cast<float>(v.z - origin.z));
    };
    // UI fill uses the same position transform and explicitly enables blending
    // and disables depth testing (vanilla materials/ui.material). It also draws
    // the remembered terrain as a translucent ghost of its 2-bit representation.
    sdk::MaterialPtr* overlay = sdk::material("ui_fill_color");
    if (overlay && !schematicCells.empty()) {
        using D = baritone::SchematicWorkspace::Difference;
        constexpr int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 2, 6, 5}};
        for (auto difference : {D::Missing, D::Wrong, D::Remove, D::Unloaded}) {
            const auto count = std::ranges::count_if(schematicCells, [=](const auto& c) { return c.difference == difference; });
            if (!count) continue;
            auto tint = schematicTint(difference, 0.04f);
            shader->set(tint);
            tess->begin(sdk::Primitive::Quad, static_cast<int>(count * 48));
            for (const auto& cell : schematicCells) {
                if (cell.difference != difference) continue;
                const Vec3d p{static_cast<double>(cell.pos.x), static_cast<double>(cell.pos.y), static_cast<double>(cell.pos.z)};
                const Vec3d corners[8] = {p, p + Vec3d{1, 0, 0}, p + Vec3d{1, 0, 1}, p + Vec3d{0, 0, 1},
                    p + Vec3d{0, 1, 0}, p + Vec3d{1, 1, 0}, p + Vec3d{1, 1, 1}, p + Vec3d{0, 1, 1}};
                for (const auto& face : faces) {
                    for (int i = 0; i < 4; ++i) put(corners[face[i]], tint);
                    for (int i = 3; i >= 0; --i) put(corners[face[i]], tint);
                }
            }
            sdk::renderMeshImmediately(screen, tess, overlay);
        }
    }
    if (s.renderCachedChunks && cachedMesh && overlay) {
        const float alpha = static_cast<float>(std::clamp(s.cachedChunksOpacity, 0.0, 1.0));
        for (auto type : {baritone::PathingBlockType::Solid, baritone::PathingBlockType::Water, baritone::PathingBlockType::Avoid}) {
            sdk::Color tint = type == baritone::PathingBlockType::Water ? sdk::Color{0.15f, 0.45f, 1, alpha}
                              : type == baritone::PathingBlockType::Avoid ? sdk::Color{1, 0.3f, 0.15f, alpha}
                              : sdk::Color{0.6f, 0.65f, 0.7f, alpha};
            std::size_t count = 0;
            for (const auto& face : *cachedMesh) if (face.type == type) ++count;
            if (!count || alpha == 0) continue;
            shader->set(tint);
            tess->begin(sdk::Primitive::Quad, static_cast<int>(count * 8));
            for (const auto& face : *cachedMesh) {
                if (face.type != type) continue;
                for (int v = 0; v < 4; ++v) put(face.vertices[v], tint);
                for (int v = 3; v >= 0; --v) put(face.vertices[v], tint);
            }
            sdk::renderMeshImmediately(screen, tess, overlay);
        }
    }

    // These position-only materials use a uniform color. Batch by color as well
    // as depth mode so every path/goal/fade keeps its requested color and alpha.
    using Batch = std::tuple<bool, float, float, float, float, bool>;
    std::map<Batch, std::vector<const Line*>> batches;
    for (const Line& line : lines)
        batches[{line.ignoreDepth, line.color.r, line.color.g, line.color.b, line.color.a, line.width > 0}].push_back(&line);
    for (const auto& [key, batch] : batches) {
        auto [ignoreDepth, r, g, b, a, thick] = key;
        sdk::MaterialPtr* mat = ignoreDepth ? overlay : sdk::material("selection_box");
        if (!mat) continue;
        shader->set({r, g, b, a});
        tess->begin(thick ? sdk::Primitive::Quad : sdk::Primitive::LineList, static_cast<int>(batch.size() * (thick ? 16 : 2)));
        for (const Line* line : batch) {
            const Line& l = *line;
            if (!thick) {
                put(l.a, l.color);
                put(l.b, l.color);
                continue;
            }
            // Two crossed ribbons, both windings (no back-face culling).
            Vec3d d = l.b - l.a;
            double len = std::sqrt(d.lengthSq());
            if (len < 1e-6) continue;
            d = d * (1.0 / len);
            Vec3d side = std::fabs(d.y) > 0.99 ? Vec3d{1, 0, 0} : Vec3d{-d.z, 0, d.x};
            side = side * (1.0 / std::sqrt(side.lengthSq()));
            Vec3d up{d.y * side.z - d.z * side.y, d.z * side.x - d.x * side.z, d.x * side.y - d.y * side.x};
            for (const Vec3d& n : {side, up}) {
                Vec3d o = n * (l.width * 0.5);
                const Vec3d q[4] = {l.a - o, l.b - o, l.b + o, l.a + o};
                for (int k = 0; k < 4; ++k) put(q[k], l.color);
                for (int k = 3; k >= 0; --k) put(q[k], l.color);
            }
        }
        sdk::renderMeshImmediately(screen, tess, mat);
    }
}

}  // namespace platform
