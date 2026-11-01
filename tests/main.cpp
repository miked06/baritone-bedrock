// Offline tests of the pathfinding core. Run: build/bin/RelWithDebInfo/baritone_tests.exe
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "TestWorld.h"
#include "TestPlayer.h"
#include "baritone/Settings.h"
#include "baritone/Baritone.h"
#include "baritone/selection/ClickSelection.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/calc/AStarPathFinder.h"
#include "baritone/pathing/goals/Goal.h"
#include "baritone/cache/CachedWorld.h"
#include "baritone/cache/CachedMesh.h"
#include "baritone/cache/ChunkPackerQueue.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/elytra/ElytraPathFinder.h"
#include "baritone/elytra/ElytraPhysics.h"
#include "baritone/schematic/Inflate.h"
#include "baritone/schematic/Orientation.h"
#include "baritone/schematic/Schematic.h"
#include "baritone/schematic/SchematicWorkspace.h"
#include "baritone/schematic/SchematicCatalog.h"
#include "baritone/api/TransferReconciliation.h"
#include "baritone/world/PortalGraph.h"
#include "baritone/schematic/QuarryPlan.h"
#include "baritone/schematic/SchematicLoadJob.h"
#include "baritone/schematic/SchematicSurvey.h"
#include "baritone/schematic/SchematicExport.h"
#include "baritone/task/TaskQueue.h"
#include "baritone/task/CheckpointWriter.h"
#include "baritone/schematic/NbtReader.h"
#include "baritone/utils/CommandLine.h"
#include "InflateData.h"

using namespace baritone;
using test::Blocks;
using test::TestWorld;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& tests() {
    static std::vector<TestCase> t;
    return t;
}
struct Register {
    Register(const char* name, std::function<void()> fn) { tests().push_back({name, std::move(fn)}); }
};
struct Failure {
    std::string message;
};

#define TEST(name)                                     \
    static void test_##name();                         \
    static Register reg_##name(#name, test_##name);    \
    static void test_##name()

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) throw Failure{std::format("{}:{}: CHECK({}) failed", __FILE__, __LINE__, #cond)}; \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                              \
    do {                                                                                                  \
        if (!(cond)) throw Failure{std::format("{}:{}: CHECK({}) failed: {}", __FILE__, __LINE__, #cond, msg)}; \
    } while (0)

// Restores the settings changed by a test.
struct SettingsGuard {
    Settings saved = Settings::get();
    ~SettingsGuard() { Settings::get() = saved; }
};

PathCalculationResult findPath(const TestWorld& world, BlockPos start, GoalPtr goal, bool hasThrowaway = false,
                               long timeoutMs = 2000) {
    PlayerAbilities abilities;
    abilities.hasThrowaway = hasThrowaway;
    auto ctx = std::make_unique<CalculationContext>(world.snapshot(), abilities, true);
    AStarPathFinder finder(start, goal, std::move(ctx), Favoring{});
    return finder.calculate(timeoutMs, timeoutMs);
}

std::string describe(const PathCalculationResult& r) {
    if (!r.path) return "no path";
    std::string s = std::format("{} positions, {:.1f} ticks:", r.path->length(), r.path->totalTicks());
    for (const auto& m : r.path->movements()) s += std::format(" [{}->{} t{}]", m.src, m.dest, static_cast<int>(m.type));
    return s;
}

bool uses(const PathCalculationResult& r, MovementType type) {
    if (!r.path) return false;
    for (const auto& m : r.path->movements())
        if (m.type == type) return true;
    return false;
}

// ---------------------------------------------------------------------------

TEST(costs_match_baritone) {
    CHECK(std::abs(costs::WALK_ONE_BLOCK_COST - 4.6328) < 0.001);
    CHECK(std::abs(costs::SPRINT_ONE_BLOCK_COST - 3.5638) < 0.001);
    // distanceToTicks(1.25) - distanceToTicks(0.25) = 6.2346 - 3.0711
    CHECK_MSG(std::abs(costs::JUMP_ONE_BLOCK_COST - 3.1635) < 0.001, std::to_string(costs::JUMP_ONE_BLOCK_COST));
    CHECK(std::abs(costs::fallCost(1) - 5.6122) < 0.01);
}

TEST(flat_walk_straight) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(10, 1, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(r.path->length() == 11, describe(r));
    CHECK(r.path->dest() == (BlockPos{10, 1, 0}));
    // sprinting the whole way
    CHECK_MSG(std::abs(r.path->totalTicks() - 10 * costs::SPRINT_ONE_BLOCK_COST) < 0.01, describe(r));
}

TEST(flat_walk_diagonal) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(8, 1, 8));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(r.path->length() == 9, describe(r));  // 8 diagonal moves
    CHECK(uses(r, MovementType::Diagonal));
}

TEST(walk_around_wall_without_breaking) {
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(5, 1, -5, 5, 3, 5, b.stone);  // wall x=5, z in -5..5, 3 high
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(10, 1, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    for (const auto& p : r.path->positions()) CHECK_MSG(!(p.x == 5 && p.z >= -5 && p.z <= 5), describe(r));
}

TEST(break_through_wall) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(5, 1, -30, 5, 2, 30, b.dirt);  // long dirt wall: breaking is cheaper than walking around
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(10, 1, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    // either through the wall or over it after breaking its top block, but not around it
    for (const auto& p : r.path->positions()) CHECK_MSG(p.z >= -2 && p.z <= 2, describe(r));
}

TEST(ascend_step) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(3, 1, -20, 20, 1, 20, b.stone);  // one block higher plateau from x=3
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(6, 2, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Ascend), describe(r));
}

TEST(descend_and_fall) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(-10, b.stone);
    w.fill(-20, -9, -20, 3, 0, 20, b.stone);  // cliff: plateau at y=0 for x<=3, ground at y=-10 after
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(8, -9, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    // 10 blocks is above maxFallHeightNoWater without a bucket: must not be a single fall
    for (const auto& m : r.path->movements()) CHECK_MSG(m.src.y - m.dest.y <= 4, describe(r));
}

TEST(fall_into_water) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(-20, b.stone);
    w.fill(-20, -19, -20, 3, 0, 20, b.stone);
    w.fill(4, -19, -3, 8, -17, 3, b.water);  // a pool 3 deep at the bottom of the cliff
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    auto r = findPath(w, {2, 1, 0}, std::make_shared<GoalBlock>(5, -17, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Fall), describe(r));
}

TEST(water_bucket_fall) {
    // off a 14 high pillar: only with a water bucket (maxFallHeightBucket), never in the nether
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(0, 1, 0, 0, 14, 0, b.stone);
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    auto withBucket = [&](bool bucket) {
        PlayerAbilities abilities;
        abilities.hasWaterBucket = bucket;
        AStarPathFinder finder({0, 15, 0}, std::make_shared<GoalBlock>(3, 1, 0),
                               std::make_unique<CalculationContext>(w.snapshot(), abilities, true), Favoring{});
        return finder.calculate(2000, 2000);
    };
    auto r = withBucket(true);
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Fall), describe(r));
    auto none = withBucket(false);
    CHECK_MSG(none.type != PathResultType::SuccessToGoal, describe(none));
}

TEST(pillar_up_with_blocks) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(0, 4, 0), /*hasThrowaway=*/true);
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Pillar), describe(r));
}

TEST(no_pillar_without_blocks) {
    const Blocks& b = Blocks::get();
    TestWorld w(2);
    w.floor(0, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(0, 4, 0), false, 300);
    CHECK_MSG(r.type != PathResultType::SuccessToGoal, describe(r));
}

TEST(swim_to_surface_without_placing_or_breaking) {
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    Settings::get().allowPlace = false;
    const Blocks& b = Blocks::get();
    TestWorld w(1);
    w.floor(0, b.stone);
    w.fill(-3, 1, -3, 3, 3, 3, b.water);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(0, 3, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Pillar), describe(r));
    PlayerAbilities abilities;
    CalculationContext open(w.snapshot(), abilities, true);
    CHECK(movement_costs::pillar(open, 0, 2, 0) < costs::COST_INF);
    w.set(0, 4, 0, b.stone);
    CalculationContext ceiling(w.snapshot(), abilities, true);
    CHECK(movement_costs::pillar(ceiling, 0, 2, 0) >= costs::COST_INF);
    w.set(0, 4, 0, b.lava);
    CalculationContext hazard(w.snapshot(), abilities, true);
    CHECK(movement_costs::pillar(hazard, 0, 2, 0) >= costs::COST_INF);
}

TEST(bridge_over_gap) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.fill(-20, 0, -1, 0, 0, 1, b.stone);   // start platform
    w.fill(6, 0, -1, 20, 0, 1, b.stone);    // end platform, gap x=1..5 over the void
    SettingsGuard guard;
    auto r = findPath(w, {-2, 1, 0}, std::make_shared<GoalBlock>(8, 1, 0), /*hasThrowaway=*/true);
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
}

TEST(parkour_gap) {
    SettingsGuard guard;
    Settings::get().allowParkour = true;
    Settings::get().allowBreak = false;
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.fill(-20, 0, -1, 0, 0, 1, b.stone);
    w.fill(3, 0, -1, 20, 0, 1, b.stone);  // 2 block gap
    auto r = findPath(w, {-2, 1, 0}, std::make_shared<GoalBlock>(6, 1, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Parkour), describe(r));
}

TEST(parkour_diagonal) {
    // a 3x3 platform, then single blocks that only meet diagonally over the void: the first
    // one is 2 blocks diagonally away from the corner (a jump), the others touch by the corners
    SettingsGuard guard;
    Settings::get().allowParkour = true;
    Settings::get().allowBreak = false;
    Settings::get().allowPlace = false;
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.fill(-1, 0, -1, 1, 0, 1, b.stone);
    for (int i = 3; i <= 6; ++i) w.fill(i, 0, i, i, 0, i, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(6, 1, 6));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    CHECK_MSG(uses(r, MovementType::Parkour), describe(r));
    // never over lava
    TestWorld lava;
    lava.fill(-1, 0, -1, 1, 0, 1, b.stone);
    lava.fill(3, 0, 3, 3, 0, 3, b.stone);
    lava.fill(2, 0, 2, 2, 0, 2, b.lava);  // right under the gap
    auto rl = findPath(lava, {0, 1, 0}, std::make_shared<GoalBlock>(3, 1, 3));
    for (const auto& m : rl.path ? rl.path->movements() : std::vector<MovementSpec>{})
        CHECK_MSG(!(m.type == MovementType::Parkour && m.src == BlockPos{1, 1, 1}), describe(rl));
    // a wall in a corner cell blocks the jump (the body sweeps through it)
    TestWorld wall;
    wall.fill(-1, 0, -1, 1, 0, 1, b.stone);
    wall.fill(3, 0, 3, 3, 0, 3, b.stone);
    wall.fill(2, 1, 1, 2, 3, 1, b.stone);
    auto rw = findPath(wall, {1, 1, 1}, std::make_shared<GoalBlock>(3, 1, 3));
    CHECK_MSG(rw.type != PathResultType::SuccessToGoal || !uses(rw, MovementType::Parkour), describe(rw));
}

TEST(avoid_lava) {
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(3, 0, -2, 7, 0, 2, b.lava);  // lava pool in the floor
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(10, 1, 0));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    for (const auto& p : r.path->positions()) {
        bool overLava = p.x >= 3 && p.x <= 7 && p.z >= -2 && p.z <= 2 && p.y == 1;
        CHECK_MSG(!overLava, describe(r));
    }
}

TEST(ladder_climb) {
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    const Blocks& b = Blocks::get();
    TestWorld w;
    w.floor(0, b.stone);
    w.fill(-20, 1, 2, 20, 10, 2, b.stone);  // wall at z=2
    w.fill(-20, 10, -20, 20, 10, 20, b.stone);  // roof at y=10 except a hole
    w.set(0, 10, 0, kAir);
    w.set(0, 10, 1, kAir);
    w.fill(0, 1, 1, 0, 9, 1, b.ladder);  // ladder against the wall
    w.set(0, 10, 1, b.ladder);
    auto r = findPath(w, {0, 1, -3}, std::make_shared<GoalBlock>(0, 11, -2));
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
}

TEST(segment_when_goal_unloaded) {
    const Blocks& b = Blocks::get();
    TestWorld w(2);  // chunks -2..1 -> x in -32..31
    w.floor(0, b.stone);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalXZ>(1000, 0));
    CHECK_MSG(r.type == PathResultType::SuccessSegment, describe(r));
    CHECK(r.path->dest().x > 20);
}

TEST(goal_semantics) {
    GoalGetToBlock g({5, 5, 5});
    CHECK(g.isInGoal(5, 6, 5));  // standing on it
    CHECK(g.isInGoal(4, 5, 5));
    CHECK(g.isInGoal(5, 4, 5));  // head in it
    CHECK(!g.isInGoal(3, 5, 5));
    GoalTwoBlocks t({0, 10, 0});
    CHECK(t.isInGoal(0, 9, 0));
    CHECK(!t.isInGoal(0, 8, 0));
    GoalNear n({0, 0, 0}, 3);
    CHECK(n.isInGoal(2, 2, 0));
    CHECK(!n.isInGoal(3, 3, 0));
    CHECK(GoalXZ::calculate(3, 4) > 0);
}

// Little-endian NBT writer, just enough to make a .mcstructure.
struct NbtWriter {
    std::vector<uint8_t> out;
    bool bigEndian = false;  // Java files
    void u8(uint8_t v) { out.push_back(v); }
    void num(uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) u8(static_cast<uint8_t>(v >> (8 * (bigEndian ? bytes - 1 - i : i))));
    }
    void i16(int16_t v) { num(static_cast<uint16_t>(v), 2); }
    void i32(int32_t v) { num(static_cast<uint32_t>(v), 4); }
    void i64(int64_t v) { num(static_cast<uint64_t>(v), 8); }
    void str(const std::string& s) {
        i16(static_cast<int16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void named(uint8_t type, const std::string& name) { u8(type), str(name); }
    void end() { u8(0); }
};

TEST(mcstructure_loading) {
    // 2x1x2 structure: stone at (0,0,0), air at (0,0,1), structure_void at (1,0,0), dirt at (1,0,1)
    NbtWriter w;
    w.named(10, "");
    w.named(3, "format_version"), w.i32(1);
    w.named(9, "size"), w.u8(3), w.i32(3), w.i32(2), w.i32(1), w.i32(2);
    w.named(10, "structure");
    {
        w.named(9, "block_indices"), w.u8(9), w.i32(2);
        w.u8(3), w.i32(4), w.i32(0), w.i32(1), w.i32(2), w.i32(3);
        w.u8(3), w.i32(4), w.i32(-1), w.i32(-1), w.i32(-1), w.i32(-1);
        w.named(9, "entities"), w.u8(10), w.i32(0);
        w.named(10, "palette");
        w.named(10, "default");
        w.named(9, "block_palette"), w.u8(10), w.i32(4);
        for (const char* name : {"minecraft:stone", "minecraft:air", "minecraft:structure_void", "minecraft:dirt"}) {
            w.named(8, "name"), w.str(name);
            w.named(10, "states"), w.end();
            w.named(3, "version"), w.i32(18100737);
            w.end();
        }
        w.named(10, "block_position_data"), w.end();
        w.end();  // default
        w.end();  // palette
    }
    w.end();  // structure
    w.named(9, "structure_world_origin"), w.u8(3), w.i32(3), w.i32(0), w.i32(0), w.i32(0);
    w.end();  // root

    auto path = (std::filesystem::temp_directory_path() / "baritone_test.mcstructure").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(w.out.data()), static_cast<std::streamsize>(w.out.size()));
    }
    SchematicPtr s = loadMcStructure(path);
    std::filesystem::remove(path);
    CHECK(s->size() == (BlockPos{2, 1, 2}));
    auto at = [&](int x, int z) { return s->desired(x, 0, z, "minecraft:grass_block"); };
    CHECK(at(0, 0).kind == DesiredBlock::Kind::Block && at(0, 0).name == "minecraft:stone");
    CHECK(at(0, 1).kind == DesiredBlock::Kind::Air);
    CHECK(at(1, 0).kind == DesiredBlock::Kind::Any);
    CHECK(at(1, 1).name == "minecraft:dirt");
}

std::string writeTemp(const std::string& name, const std::vector<uint8_t>& bytes) {
    auto path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

TEST(inflate_gzip_and_zlib) {
    auto run = [](const uint8_t* p, std::size_t n) {
        auto out = decompressIfNeeded(std::vector<uint8_t>(p, p + n));
        return std::string(out.begin(), out.end());
    };
    CHECK(run(kGzipDynamic, sizeof kGzipDynamic) == kInflateText);
    std::string stored;
    for (int i = 0; i < 20; ++i) stored += "hello stored block ";
    CHECK(run(kGzipStored, sizeof kGzipStored) == stored);
    CHECK(run(kZlibFixed, sizeof kZlibFixed) == "abcabcabcabd");
    std::vector<uint8_t> plain{10, 0, 0, 0};  // not compressed: unchanged
    CHECK(decompressIfNeeded(plain) == plain);
}

TEST(sponge_schem_loading) {
    NbtWriter w;
    w.bigEndian = true;
    w.named(10, "Schematic");
    w.named(3, "Version"), w.i32(2);
    w.named(2, "Width"), w.i16(2);
    w.named(2, "Height"), w.i16(1);
    w.named(2, "Length"), w.i16(2);
    w.named(10, "Palette");
    w.named(3, "minecraft:stone"), w.i32(0);
    w.named(3, "minecraft:oak_stairs[facing=north,half=bottom]"), w.i32(1);
    w.named(3, "minecraft:air"), w.i32(2);
    w.named(3, "minecraft:cobweb"), w.i32(3);
    w.end();
    w.named(7, "BlockData"), w.i32(4), w.u8(0), w.u8(1), w.u8(2), w.u8(3);  // (y * L + z) * W + x
    w.end();
    auto path = writeTemp("baritone_test.schem", w.out);
    SchematicPtr s = loadSchematic(path);
    std::filesystem::remove(path);
    CHECK(s->size() == (BlockPos{2, 1, 2}));
    CHECK(s->desired(0, 0, 0, "").name == "minecraft:stone");
    CHECK(s->desired(1, 0, 0, "").name == "minecraft:oak_stairs");
    CHECK(s->desired(0, 0, 1, "").kind == DesiredBlock::Kind::Air);
    CHECK(s->desired(1, 0, 1, "").name == "minecraft:web");
}

TEST(litematic_loading) {
    // 30x1x1 region with a negative x size, 5 palette entries -> 3 bits, entries cross long boundaries
    const char* names[] = {"minecraft:air", "minecraft:stone", "minecraft:red_bed", "minecraft:dirt", "minecraft:spawner"};
    std::vector<int> values;
    for (int i = 0; i < 30; ++i) values.push_back((i * 7 + 3) % 5);
    std::vector<uint64_t> longs(2, 0);
    for (int i = 0; i < 30; ++i) {
        uint64_t bit = static_cast<uint64_t>(i) * 3;
        longs[bit >> 6] |= static_cast<uint64_t>(values[i]) << (bit & 63);
        if ((bit & 63) > 61) longs[(bit >> 6) + 1] |= static_cast<uint64_t>(values[i]) >> (64 - (bit & 63));
    }
    NbtWriter w;
    w.bigEndian = true;
    w.named(10, "");
    w.named(3, "Version"), w.i32(6);
    w.named(10, "Regions");
    w.named(10, "house");
    w.named(10, "Position"), w.named(3, "x"), w.i32(5), w.named(3, "y"), w.i32(0), w.named(3, "z"), w.i32(0), w.end();
    w.named(10, "Size"), w.named(3, "x"), w.i32(-30), w.named(3, "y"), w.i32(1), w.named(3, "z"), w.i32(1), w.end();
    w.named(9, "BlockStatePalette"), w.u8(10), w.i32(5);
    for (const char* n : names) {
        w.named(8, "Name"), w.str(n);
        if (std::string(n) == "minecraft:red_bed") w.named(10, "Properties"), w.named(8, "part"), w.str("head"), w.end();
        w.end();
    }
    w.named(12, "BlockStates"), w.i32(2), w.i64(static_cast<int64_t>(longs[0])), w.i64(static_cast<int64_t>(longs[1]));
    w.end();  // house
    w.end();  // Regions
    w.end();  // root
    auto path = writeTemp("baritone_test.litematic", w.out);
    SchematicPtr s = loadSchematic(path);
    std::filesystem::remove(path);
    CHECK(s->size() == (BlockPos{30, 1, 1}));
    const char* bedrock[] = {"", "minecraft:stone", "minecraft:bed", "minecraft:dirt", "minecraft:mob_spawner"};
    for (int x = 0; x < 30; ++x) {
        DesiredBlock d = s->desired(x, 0, 0, "");
        if (values[x] == 0) CHECK_MSG(d.kind == DesiredBlock::Kind::Air, std::format("x={}", x));
        else CHECK_MSG(d.name == bedrock[values[x]], std::format("x={} got {}", x, d.name));
    }
}

TEST(legacy_schematic_loading) {
    NbtWriter w;
    w.bigEndian = true;
    w.named(10, "Schematic");
    w.named(2, "Width"), w.i16(3);
    w.named(2, "Height"), w.i16(1);
    w.named(2, "Length"), w.i16(1);
    w.named(8, "Materials"), w.str("Alpha");
    w.named(7, "Blocks"), w.i32(3), w.u8(53), w.u8(35), w.u8(17);  // oak stairs, wool, log
    w.named(7, "Data"), w.i32(3), w.u8(7), w.u8(14), w.u8(4 | 2);   // north upside down, red, birch along x
    w.end();
    auto path = writeTemp("baritone_test.schematic", w.out);
    SchematicPtr s = loadSchematic(path);
    std::filesystem::remove(path);
    CHECK(s->size() == (BlockPos{3, 1, 1}));
    DesiredBlock stairs = s->desired(0, 0, 0, "");
    CHECK(stairs.name == "minecraft:oak_stairs");
    CHECK(requiredFacing(stairs) == Direction::North);
    CHECK(placementGivesOrientation(stairs, {Direction::West, true, Direction::North}));
    CHECK(s->desired(1, 0, 0, "").name == "minecraft:red_wool");
    DesiredBlock log = s->desired(2, 0, 0, "");
    CHECK(log.name == "minecraft:birch_log");
    CHECK(placementGivesOrientation(log, {Direction::West, false, Direction::North}));
}

TEST(orientation_rules) {
    DesiredBlock stairs = javaToBedrock("minecraft:oak_stairs[facing=north,half=top,shape=straight]");
    CHECK(stairs.name == "minecraft:oak_stairs");
    CHECK(requiredFacing(stairs) == Direction::North);
    CHECK(placementGivesOrientation(stairs, {Direction::West, true, Direction::North}));
    CHECK(!placementGivesOrientation(stairs, {Direction::West, false, Direction::North}));  // bottom half
    CHECK(!placementGivesOrientation(stairs, {Direction::Down, false, Direction::North}));
    CHECK(!placementGivesOrientation(stairs, {Direction::West, true, Direction::East}));

    DesiredBlock log = javaToBedrock("minecraft:oak_log[axis=x]");
    CHECK(placementGivesOrientation(log, {Direction::East, false, Direction::South}));
    CHECK(!placementGivesOrientation(log, {Direction::Down, false, Direction::South}));
    CHECK(!requiredFacing(log));

    DesiredBlock furnace = javaToBedrock("minecraft:furnace[facing=south,lit=false]");
    CHECK(requiredFacing(furnace) == Direction::North);  // faces the player, who looks north
    CHECK(placementGivesOrientation(furnace, {Direction::Down, false, Direction::North}));

    DesiredBlock slab = javaToBedrock("minecraft:stone_brick_slab[type=top]");
    CHECK(placementGivesOrientation(slab, {Direction::Up, true, Direction::North}));  // under a block
    CHECK(javaToBedrock("minecraft:oak_slab[type=double]").name == "minecraft:oak_double_slab");

    BlockProps placed;
    placed.name = "minecraft:oak_stairs";
    placed.states = {{"weirdo_direction", "3"}, {"upside_down_bit", "1"}};
    CHECK(blockMatches(stairs, placed, false));
    placed.states[0].second = "0";
    CHECK(!blockMatches(stairs, placed, false));
    CHECK(blockMatches(stairs, placed, true));
    CHECK(horizontalFacing(0) == Direction::South && horizontalFacing(90) == Direction::West &&
          horizontalFacing(-90) == Direction::East && horizontalFacing(180) == Direction::North);
}

TEST(composite_and_map_art) {
    auto stone = DesiredBlock::block("minecraft:stone");
    CompositeSchematic c({5, 1, 1}, {{{0, 0, 0}, std::make_shared<FillSchematic>(BlockPos{2, 1, 1}, stone)},
                                     {{4, 0, 0}, std::make_shared<FillSchematic>(BlockPos{1, 1, 1}, DesiredBlock::air())}});
    CHECK(c.desired(1, 0, 0, "").kind == DesiredBlock::Kind::Block);
    CHECK(c.desired(2, 0, 0, "").kind == DesiredBlock::Kind::Any);
    CHECK(c.desired(4, 0, 0, "").kind == DesiredBlock::Kind::Air);
    MapArtSchematic art(std::make_shared<FillSchematic>(BlockPos{1, 3, 1}, stone));
    CHECK(art.desired(0, 2, 0, "").kind == DesiredBlock::Kind::Block);
    CHECK(art.desired(0, 1, 0, "").kind == DesiredBlock::Kind::Any);
}

TEST(selection_schematics) {
    WallsSchematic walls({3, 2, 3}, DesiredBlock::block("minecraft:stone"));
    CHECK(walls.desired(0, 1, 1, "").kind == DesiredBlock::Kind::Block);
    CHECK(walls.desired(1, 0, 1, "").kind == DesiredBlock::Kind::Any);
    ShellSchematic shell({3, 3, 3}, DesiredBlock::block("minecraft:stone"));
    CHECK(shell.desired(1, 0, 1, "").kind == DesiredBlock::Kind::Block);
    CHECK(shell.desired(1, 1, 1, "").kind == DesiredBlock::Kind::Any);
    ReplaceSchematic replace({1, 1, 1}, {"minecraft:dirt"}, DesiredBlock::block("minecraft:stone"));
    CHECK(replace.desired(0, 0, 0, "minecraft:dirt").kind == DesiredBlock::Kind::Block);
    CHECK(replace.desired(0, 0, 0, "minecraft:air").kind == DesiredBlock::Kind::Block);
    replace.reset();
    CHECK(replace.desired(0, 0, 0, "minecraft:sand").kind == DesiredBlock::Kind::Any);
    GoalAdjacent adj({5, 5, 5});
    CHECK(!adj.isInGoal(5, 5, 5));  // standing in the spot to fill
    CHECK(!adj.isInGoal(5, 4, 5));  // head in it
    CHECK(adj.isInGoal(4, 5, 5));
    CHECK(adj.isInGoal(5, 6, 5));  // on top, place below
}

TEST(builder_never_breaks_correct_blocks) {
    const Blocks& b = Blocks::get();
    TestWorld w(2);
    w.floor(0, b.stone);
    w.fill(3, 1, -2, 3, 2, 2, b.dirt);  // a dirt wall that is part of the build
    PlayerAbilities abilities;
    auto ctx = std::make_unique<CalculationContext>(w.snapshot(), abilities, true);
    auto sch = std::make_shared<FillSchematic>(BlockPos{1, 2, 5}, DesiredBlock::block("minecraft:dirt"));
    BlockPos origin{3, 1, -2};
    ctx->breakOverride = [sch, origin](int x, int y, int z, const BlockProps& cur) {
        int rx = x - origin.x, ry = y - origin.y, rz = z - origin.z;
        if (!sch->inBounds(rx, ry, rz)) return 1.0;
        DesiredBlock d = sch->desired(rx, ry, rz, cur.name);
        return d.kind == DesiredBlock::Kind::Block && d.name == cur.name ? costs::COST_INF : 1.0;
    };
    AStarPathFinder finder({0, 1, 0}, std::make_shared<GoalBlock>(6, 1, 0), std::move(ctx), Favoring{});
    auto r = finder.calculate(2000, 2000);
    CHECK_MSG(r.path, describe(r));
    for (const auto& m : r.path->movements()) CHECK_MSG(m.dest.x != 3 || std::abs(m.dest.z) > 2, describe(r));
}

// Fresh temporary directory for a cache, removed by the destructor.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / std::format("baritone_cache_test_{}", std::rand());
        std::filesystem::remove_all(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

StateId diamondOre() {
    static StateId id = [] {
        BlockProps p;
        p.name = "minecraft:diamond_ore";
        p.collides = p.fullCube = p.canPlaceAgainst = true;
        p.hardness = 3;
        p.bestTool = Tool::Pickaxe;
        p.requiresTool = true;
        return BlockRegistry::get().add(p);
    }();
    return id;
}

std::shared_ptr<CachedWorld> makeCache(const std::filesystem::path& dir) {
    const Blocks& b = Blocks::get();
    return std::make_shared<CachedWorld>(dir, -64, 384, std::array<StateId, 4>{kAir, b.water, b.lava, b.stone});
}

TEST(chunk_packer_queue_deduplicates_limits_and_keeps_latest_snapshot) {
    ChunkPackerQueue queue;
    queue.setLimit(2);
    auto a = std::make_shared<ChunkData>(0, 0, -64, 384);
    auto b = std::make_shared<ChunkData>(1, 0, -64, 384);
    auto updatedA = std::make_shared<ChunkData>(0, 0, -64, 384);
    CHECK(queue.push(a));
    CHECK(queue.push(b));
    CHECK(queue.push(updatedA));  // deduplicate even when full
    CHECK(queue.size() == 2);
    CHECK(!queue.push(std::make_shared<ChunkData>(2, 0, -64, 384)));
    CHECK(queue.take() == updatedA);
    CHECK(queue.take() == b);
    CHECK(!queue.take());
    queue.push(a);
    queue.push(b);
    queue.setLimit(1);
    CHECK(queue.size() == 1 && queue.take() == b);
    queue.setLimit(0);
    CHECK(!queue.push(a) && queue.size() == 0);
}

TEST(flight_cache_culling_preserves_files_and_unsaved_regions) {
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    auto near = std::make_shared<CachedChunk>(0, 0, -64, 384);
    auto far = std::make_shared<CachedChunk>(64, 0, -64, 384);
    far->setType(0, 1, 0, PathingBlockType::Water);
    cache->put(near);
    cache->put(far);
    CHECK(cache->loadedRegions() == 2);
    cache->pruneOutside({0, 1, 0}, 32);
    CHECK(cache->loadedRegions() == 2);  // dirty regions stay in memory
    cache->saveNow();
    cache->pruneOutside({0, 1, 0}, 32);
    CHECK(cache->loadedRegions() == 1);
    CHECK(cache->chunk(64, 0)->type(0, 1, 0) == PathingBlockType::Water);
    // Force a write failure with a directory where the region file should be.
    std::filesystem::remove(tmp.path / "r.2.0.bbr");
    std::filesystem::create_directory(tmp.path / "r.2.0.bbr");
    auto changed = std::make_shared<CachedChunk>(*far);
    changed->setType(0, 1, 0, PathingBlockType::Avoid);
    cache->put(changed);
    cache->saveNow();
    cache->pruneOutside({0, 1, 0}, 32);
    CHECK(cache->loadedRegions() == 2);
    CHECK(cache->chunk(64, 0)->type(0, 1, 0) == PathingBlockType::Avoid);
}

TEST(cache_pack_and_disk_roundtrip) {
    const Blocks& b = Blocks::get();
    TempDir tmp;
    TestWorld w(1);
    w.floor(0, b.stone);
    w.set(3, 1, 4, b.water);
    w.set(4, 1, 4, b.flowingWater);
    w.set(5, 1, 4, b.lava);
    w.set(6, -20, 7, diamondOre());
    {
        auto cache = makeCache(tmp.path);
        for (int cx = -1; cx < 1; ++cx)
            for (int cz = -1; cz < 1; ++cz) cache->put(CachedChunk::pack(*w.snapshot()->chunk(cx, cz), -64, 384, 42));
        cache->saveNow();
    }
    auto cache = makeCache(tmp.path);  // reads the files back
    auto c = cache->chunk(0, 0);
    CHECK(c != nullptr);
    CHECK(cache->chunk(5, 5) == nullptr);
    CHECK(c->timestamp() == 42);
    CHECK(c->type(0, 0, 0) == PathingBlockType::Solid);
    CHECK(c->type(0, 1, 0) == PathingBlockType::Air);
    CHECK(c->type(3, 1, 4) == PathingBlockType::Water);
    CHECK(c->type(4, 1, 4) == PathingBlockType::Avoid);  // flowing water
    CHECK(c->type(5, 1, 4) == PathingBlockType::Avoid);
    CHECK(c->special().contains("minecraft:diamond_ore"));
    CHECK(c->special().at("minecraft:diamond_ore").front() == (BlockPos{6, -20, 7}));
    CHECK(cache->isCached(-1, -1));
}

TEST(path_through_cached_chunks) {
    const Blocks& b = Blocks::get();
    SettingsGuard guard;
    Settings::get().allowBreak = false;
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    {
        TestWorld far(3);  // x in -48..47, seen earlier
        far.floor(0, b.stone);
        far.fill(30, 1, -48, 30, 3, 40, b.stone);  // a wall with a gap at z > 40
        for (auto [cx, cz] : far.snapshot()->loadedChunks()) cache->put(CachedChunk::pack(*far.snapshot()->chunk(cx, cz), -64, 384, 0));
    }
    TestWorld w(1);  // only x in -16..15 is loaded now
    w.floor(0, b.stone);
    w.setCache(cache);
    auto r = findPath(w, {0, 1, 0}, std::make_shared<GoalBlock>(40, 1, 0), false, 4000);
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
    bool goesAround = false;
    for (const auto& m : r.path->movements()) {
        if (m.dest.x == 30) goesAround |= m.dest.z > 40;
        if (m.dest.x >= 16) CHECK(!m.calculatedWhileLoaded);
    }
    CHECK_MSG(goesAround, describe(r));
}

TEST(search_world_uses_cache) {
    const Blocks& b = Blocks::get();
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    {
        TestWorld far(4);
        far.floor(0, b.stone);
        far.set(50, 5, -40, diamondOre());
        far.set(2, 5, 2, diamondOre());  // also in the loaded area: the loaded scan finds it
        for (auto [cx, cz] : far.snapshot()->loadedChunks()) cache->put(CachedChunk::pack(*far.snapshot()->chunk(cx, cz), -64, 384, 0));
    }
    TestWorld w(1);
    w.floor(0, b.stone);
    w.set(2, 5, 2, diamondOre());
    w.setCache(cache);
    auto found = searchWorld(*w.snapshot(), BlockFilter({"diamond_ore"}), {0, 1, 0}, 10, 8);
    CHECK_MSG(found.size() == 2, std::format("{} found", found.size()));
    CHECK(found[0] == (BlockPos{2, 5, 2}));
    CHECK(found[1] == (BlockPos{50, 5, -40}));
}

TEST(search_world_cache_threshold_and_stale_positions) {
    SettingsGuard guard;
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    TestWorld world(1);
    world.set(2, 1, 2, diamondOre());
    cache->put(CachedChunk::pack(*world.snapshot()->chunk(0, 0), -64, 384, 0));
    world.set(2, 1, 2, kAir);  // stale cached location: must be rejected
    world.set(3, 1, 3, diamondOre());  // discovered since last packing
    world.setCache(cache);
    Settings::get().extendCacheOnThreshold = false;
    CHECK(searchWorld(*world.snapshot(), BlockFilter({"diamond_ore"}), {0, 1, 0}, 10, 1).empty());
    Settings::get().extendCacheOnThreshold = true;
    auto found = searchWorld(*world.snapshot(), BlockFilter({"diamond_ore"}), {0, 1, 0}, 10, 1);
    CHECK(found == std::vector<BlockPos>({{3, 1, 3}}));
    CHECK(searchWorld(*world.snapshot(), BlockFilter({"diamond_ore"}), {0, 1, 0}, 0, 1).empty());
}

TEST(cached_mesh_exposes_surfaces_and_respects_loaded_chunks_and_budget) {
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    auto chunk = std::make_shared<CachedChunk>(0, 0, -64, 384);
    chunk->setType(0, 1, 0, PathingBlockType::Solid);
    cache->put(chunk);
    TestWorld empty(0);
    CHECK(buildCachedMesh(*cache, *empty.snapshot(), {0, 1, 0}, 0).size() == 6);
    auto adjacent = std::make_shared<CachedChunk>(0, 0, -64, 384);
    adjacent->setType(0, 1, 0, PathingBlockType::Solid);
    adjacent->setType(1, 1, 0, PathingBlockType::Solid);
    cache->put(adjacent);
    CHECK(buildCachedMesh(*cache, *empty.snapshot(), {0, 1, 0}, 0).size() == 10);
    CHECK(buildCachedMesh(*cache, *empty.snapshot(), {0, 1, 0}, 0, 3).size() == 3);
    TestWorld loaded(1);
    CHECK(buildCachedMesh(*cache, *loaded.snapshot(), {0, 1, 0}, 0).empty());
    auto boundary = std::make_shared<CachedChunk>(0, 0, -64, 384);
    boundary->setType(15, 1, 0, PathingBlockType::Solid);
    auto neighbor = std::make_shared<CachedChunk>(1, 0, -64, 384);
    neighbor->setType(0, 1, 0, PathingBlockType::Solid);
    cache->put(boundary);
    cache->put(neighbor);
    CHECK(buildCachedMesh(*cache, *empty.snapshot(), {15, 1, 0}, 1).size() == 10);
}

TEST(elytra_physics) {
    using namespace elytra;
    // level glide: slowly loses height, keeps a steady forward speed
    FlightState s{{0, 200, 0}, {0, 0, 1}};
    for (int i = 0; i < 200; ++i) s = step(s, {0, 0}, false);
    CHECK(s.pos.y < 200 && s.pos.y > 150);
    CHECK(s.pos.z > 100);
    // diving speeds up, a firework pushes to ~1.6 blocks/tick
    FlightState dive{{0, 200, 0}, {0, 0, 1}};
    for (int i = 0; i < 40; ++i) dive = step(dive, {0, 60}, false);
    CHECK(std::sqrt(dive.vel.lengthSq()) > 1.0);
    FlightState boost{{0, 200, 0}, {0, 0, 0.5}};
    for (int i = 0; i < 30; ++i) boost = step(boost, {0, 0}, true);
    double v = std::sqrt(boost.vel.lengthSq());
    CHECK_MSG(v > 1.4 && v < 2.0, std::format("{}", v));
}

TEST(elytra_path_around_wall) {
    const Blocks& b = Blocks::get();
    TestWorld w(3);
    w.floor(0, b.stone);
    w.fill(10, 1, -48, 10, 60, 40, b.stone);  // a high wall with a gap at z > 40
    // This checks route/collision correctness, not a wall-clock performance SLA.
    // Leave headroom for sanitizer overhead and a running Minecraft client.
    const auto started = std::chrono::steady_clock::now();
    auto r = elytra::ElytraPathFinder(w.snapshot()).calculate({0, 20, 0}, {30, 20, 0}, 2.0, 10000);
    std::printf("      elytra wall: %d nodes, %.0f ms\n", r.nodes,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    CHECK_MSG(r.reachesGoal, std::format("{} nodes", r.nodes));
    bool around = std::ranges::any_of(r.path, [](const BlockPos& p) { return p.x == 10 && (p.z > 40 || p.y > 61); });
    CHECK(around);
    BlockStateInterface bsi(w.snapshot());
    for (const BlockPos& p : r.path) CHECK(!elytra::collides(bsi, Vec3d::center(p)));
    // a dive straight into the floor is predicted
    CHECK(!elytra::simulate(bsi, {{0, 5, 0}, {0, -1, 1}}, {0, 80}, 20, 0));
    CHECK(elytra::simulate(bsi, {{0, 30, 0}, {0, 0, 1}}, {0, 0}, 20, 0).has_value());
}

TEST(mine_filter_ore_families) {
    BlockFilter diamonds({"diamond_ore"});
    CHECK(diamonds.matchesName("minecraft:diamond_ore"));
    CHECK(diamonds.matchesName("minecraft:deepslate_diamond_ore"));
    CHECK(diamonds.dropNames().contains("minecraft:diamond"));
    BlockFilter item({"diamond"});  // the item name means its ores
    CHECK(item.matchesName("minecraft:deepslate_diamond_ore"));
    CHECK(!item.matchesName("minecraft:diamond"));
    BlockFilter redstone({"redstone_ore"});
    CHECK(redstone.matchesName("minecraft:lit_deepslate_redstone_ore"));
    BlockFilter gold({"gold"});
    CHECK(gold.matchesName("minecraft:deepslate_gold_ore"));
    BlockFilter goldOre({"gold_ore"});  // the block name: only that ore and its deepslate variant
    CHECK(!goldOre.matchesName("minecraft:nether_gold_ore"));
    CHECK(gold.matchesName("minecraft:gold_ore"));
    BlockFilter netherGold({"gold"});
    CHECK(netherGold.matchesName("minecraft:nether_gold_ore"));  // "gold" in the nether too
    BlockFilter quartz({"nether_quartz_ore"});                     // Java name
    CHECK(quartz.matchesName("minecraft:quartz_ore"));
    BlockFilter quartzItem({"quartz"});
    CHECK(quartzItem.matchesName("minecraft:quartz_ore"));
    BlockFilter debris({"netherite"});
    CHECK(debris.matchesName("minecraft:ancient_debris"));
    BlockFilter stone({"stone"});
    CHECK(stone.names().size() == 1 && stone.matchesName("minecraft:stone"));
}

TEST(mine_branch_goal_digs_down) {
    // creative (everything breaks at once), no ore known: down to the mining level, then tunnels
    const Blocks& b = Blocks::get();
    TestWorld w(6);
    w.fill(-96, -40, -96, 95, 0, 95, b.stone);
    auto creativePath = [&](BlockPos start, GoalPtr goal) {
        PlayerAbilities abilities;
        abilities.breakSpeed = [](const BlockProps& p) { return p.hardness < 0 || p.unbreakable ? 0.0 : 1.0; };
        AStarPathFinder finder(start, std::move(goal), std::make_unique<CalculationContext>(w.snapshot(), abilities, true),
                               Favoring{});
        return finder.calculate(2000, 2000);
    };
    // (a segment is enough: the bot recalculates from its end until it is at that level)
    auto down = creativePath({0, 1, 0}, std::make_shared<GoalYLevel>(-30));
    CHECK_MSG(down.path != nullptr && down.path->dest().y <= -10, describe(down));
    BlockPos branch{0, -30, 0};
    w.fill(0, -30, 0, 0, -29, 0, kAir);
    auto tunnel = creativePath(branch, std::make_shared<GoalRunAway>(1, -30, std::vector<BlockPos>{branch}, true));
    CHECK_MSG(tunnel.path != nullptr && tunnel.path->length() > 3, describe(tunnel));
    CHECK_MSG(tunnel.path->dest().y == -30, describe(tunnel));
}

TEST(sphere_and_cylinder_masks) {
    // Java SphereMask / CylinderMask
    auto sphere = sphereMask({5, 5, 5}, true);
    CHECK(sphere(2, 2, 2));
    CHECK(!sphere(0, 0, 0));
    auto hollowSphere = sphereMask({5, 5, 5}, false);
    CHECK(!hollowSphere(2, 2, 2));
    CHECK(hollowSphere(2, 0, 2));
    auto cylinder = cylinderMask({5, 3, 5}, true, 'y');
    CHECK(cylinder(2, 0, 2) && cylinder(2, 2, 2));
    CHECK(!cylinder(0, 1, 0));
    auto hollowCylinder = cylinderMask({5, 3, 5}, false, 'y');
    CHECK(!hollowCylinder(2, 1, 2));
    CHECK(hollowCylinder(1, 1, 2));
}

TEST(schematic_rotation_and_mirror) {
    // 2 wide (x) by 3 long (z), dirt in the corner (1, 0, 2)
    std::vector<int> indices(6, 0);
    indices[(1 * 1 + 0) * 3 + 2] = 1;
    auto base = std::make_shared<StaticSchematic>(BlockPos{2, 1, 3},
                                                  std::vector<DesiredBlock>{DesiredBlock::block("minecraft:stone"),
                                                                            DesiredBlock::block("minecraft:dirt")},
                                                  indices);
    RotatedSchematic rotated(base, 1);  // a quarter turn clockwise
    CHECK(rotated.size() == (BlockPos{3, 1, 2}));
    CHECK(rotated.desired(0, 0, 1, "").name == "minecraft:dirt");
    CHECK(rotated.desired(2, 0, 1, "").name == "minecraft:stone");
    RotatedSchematic back(std::make_shared<RotatedSchematic>(base, 1), 3);
    CHECK(back.desired(1, 0, 2, "").name == "minecraft:dirt");
    MirroredSchematic mirrored(base, 1);  // LEFT_RIGHT flips z
    CHECK(mirrored.desired(1, 0, 0, "").name == "minecraft:dirt");

    DesiredBlock stairs = DesiredBlock::block("minecraft:oak_stairs");
    stairs.states = {{"weirdo_direction", "0"}};  // east
    CHECK(rotateBlock(stairs, 1).states[0].second == "2");  // south
    CHECK(rotateBlock(stairs, 2).states[0].second == "1");  // west
    DesiredBlock log = DesiredBlock::block("minecraft:oak_log");
    log.states = {{"pillar_axis", "x"}};
    CHECK(rotateBlock(log, 1).states[0].second == "z");
    CHECK(rotateBlock(log, 2).states[0].second == "x");
    DesiredBlock furnace = DesiredBlock::block("minecraft:furnace");
    furnace.states = {{"minecraft:cardinal_direction", "north"}};
    CHECK(mirrorBlock(furnace, 1).states[0].second == "south");
    CHECK(mirrorBlock(furnace, 2).states[0].second == "north");
    DesiredBlock observer = DesiredBlock::block("minecraft:observer");
    observer.states = {{"facing_direction", "5"}};  // east
    CHECK(mirrorBlock(observer, 2).states[0].second == "4");  // west
    CHECK(rotateBlock(observer, 1).states[0].second == "3");  // south
}

TEST(path_static_cutoff) {
    // Java PathBase.staticCutoff: (length - min) * factor + min positions when the end isn't the goal
    std::vector<BlockPos> positions;
    std::vector<MovementSpec> movements;
    for (int i = 0; i < 100; ++i) {
        positions.push_back({i, 1, 0});
        if (i > 0) movements.push_back({MovementType::Traverse, {i - 1, 1, 0}, {i, 1, 0}, 4.633, true});
    }
    auto path = Path::fromParts(positions, movements, std::make_shared<GoalBlock>(500, 1, 0), 0);
    GoalBlock far(500, 1, 0), end(99, 1, 0);
    CHECK(path->staticCutoff(&far, 0.9, 30)->length() == 93);
    CHECK(path->staticCutoff(&end, 0.9, 30)->length() == 100);
    CHECK(path->staticCutoff(&far, 0.9, 200)->length() == 100);
    CHECK(path->staticCutoff(&far, 0.9, 30)->movements().size() == 92);
}

TEST(toolset_break_speed) {
    // Java ToolSet: the right tool, its tier, efficiency (level^2 + 1)
    const BlockProps& stone = BlockRegistry::get().props(Blocks::get().stone);
    HotbarStack hand;
    HotbarStack pickaxe{"minecraft:diamond_pickaxe", 1, false, Tool::Pickaxe, 4, 8.0f};
    HotbarStack fast = pickaxe;
    fast.efficiency = 5;
    double h = InventoryBehavior::breakSpeed(hand, stone);
    double p = InventoryBehavior::breakSpeed(pickaxe, stone);
    double f = InventoryBehavior::breakSpeed(fast, stone);
    CHECK(p > h * 20);
    CHECK(std::abs(f / p - 34.0 / 8.0) < 1e-9);
    CHECK(InventoryBehavior::materialCost(pickaxe) == 4);
    CHECK(InventoryBehavior::materialCost(HotbarStack{"minecraft:wooden_pickaxe", 1}) == 0);
    CHECK(InventoryBehavior::materialCost(HotbarStack{"minecraft:stick", 1}) == -1);
}

TEST(manual_tool_path_cost_and_snapshot) {
    SettingsGuard guard;
    test::TestPlayer player;
    player.inventory[1] = {"minecraft:diamond_pickaxe", 1, false, Tool::Pickaxe, 4, 8.0f};
    InventoryBehavior inventory(player);
    const auto& stone = BlockRegistry::get().props(Blocks::get().stone);
    Settings::get().autoTool = false;
    auto manual = inventory.abilities();
    Settings::get().autoTool = true;
    auto automatic = inventory.abilities();
    CHECK(automatic.breakSpeed(stone) > manual.breakSpeed(stone) * 20);
    // A calculation keeps the original held item even after the real player switches.
    player.selected = 1;
    CHECK(manual.breakSpeed(stone) == InventoryBehavior::breakSpeed({}, stone));
    Settings::get().autoTool = false;
    CHECK(inventory.abilities().breakSpeed(stone) == automatic.breakSpeed(stone));
}

TEST(inventory_retrieves_enchanted_tool_and_preserves_worn_tool) {
    SettingsGuard guard;
    Settings::get().allowInventory = true;
    Settings::get().allowWaterBucketFall = false;
    Settings::get().itemSaver = true;
    test::TestPlayer player;
    player.inventory[0] = {"minecraft:iron_pickaxe", 1, false, Tool::Pickaxe, 3, 6.0f};
    player.inventory[9] = {"minecraft:wooden_pickaxe", 1, false, Tool::Pickaxe, 1, 2.0f};
    player.inventory[9].efficiency = 5;
    player.inventory[10] = {"minecraft:netherite_pickaxe", 1, false, Tool::Pickaxe, 5, 9.0f};
    player.inventory[10].efficiency = 5;
    player.inventory[10].maxDamage = 2031;
    player.inventory[10].damage = 2030;
    InventoryBehavior inventory(player);
    inventory.setStationaryCheck([] { return true; });
    inventory.tick();
    CHECK(player.lastSwap == std::make_optional(std::pair{9, 0}));
    CHECK(player.inventory[10].damage == 2030);
}

TEST(depth_strider_water_cost) {
    TestWorld world(1);
    test::TestPlayer player;
    InventoryBehavior inventory(player);
    double previous = costs::WALK_ONE_IN_WATER_COST;
    for (int level = 0; level <= 3; ++level) {
        player.equipment[3].depthStrider = level;
        CalculationContext context(world.snapshot(), inventory.abilities(), true);
        CHECK(context.waterWalkSpeed <= previous);
        CHECK(context.waterWalkSpeed >= costs::WALK_ONE_BLOCK_COST);
        previous = context.waterWalkSpeed;
    }
    CHECK(std::abs(previous - costs::WALK_ONE_BLOCK_COST) < 1e-9);
}

TEST(block_state_selectors_scan_only_matching_states) {
    TestWorld world(1);
    BlockProps log;
    log.name = "minecraft:oak_log";
    log.states = {{"pillar_axis", "y"}};
    auto y = BlockRegistry::get().add(log);
    log.states = {{"pillar_axis", "x"}};
    auto x = BlockRegistry::get().add(log);
    world.set(1, 1, 0, y);
    world.set(2, 1, 0, x);
    auto found = scanLoadedChunks(*world.snapshot(), BlockFilter({"oak_log[pillar_axis=y]"}), {0, 1, 0}, 10, 1);
    CHECK(found == std::vector<BlockPos>({{1, 1, 0}}));
    CHECK(scanLoadedChunks(*world.snapshot(), BlockFilter({"oak_log"}), {0, 1, 0}, 10, 1).size() == 2);
    CHECK(scanLoadedChunks(*world.snapshot(), BlockFilter({"oak_log[pillar_axis=z]"}), {0, 1, 0}, 10, 1).empty());
    CHECK(BlockSelector::parse("oak_door[open_bit=true]").states.front().second == "1");
    for (const std::string input : {"", "oak_log[", "oak_log[axis]", "oak_log[axis=]", "oak_log[axis=x,axis=y]", "oak_log[axis=x,]"}) {
        bool rejected = false;
        try { BlockSelector::parse(input); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK_MSG(rejected, input);
    }
}

TEST(replace_state_mask_survives_breaking_and_resets_through_wrappers) {
    auto replace = std::make_shared<ReplaceSchematic>(BlockPos{1, 1, 1},
        std::unordered_set<std::string>{"oak_log[pillar_axis=y]"}, DesiredBlock::block("minecraft:stone"));
    auto mirrored = std::make_shared<MirroredSchematic>(replace, 1);
    auto rotated = std::make_shared<RotatedSchematic>(mirrored, 1);
    auto masked = std::make_shared<MaskSchematic>(rotated, [](int, int, int) { return true; });
    CompositeSchematic composite({1, 1, 1}, {{{0, 0, 0}, masked}});
    BlockProps current;
    current.name = "minecraft:oak_log";
    current.states = {{"pillar_axis", "y"}};
    CHECK(composite.desired(0, 0, 0, current).name == "minecraft:stone");
    current.name = "minecraft:air";
    current.states.clear();
    CHECK(composite.desired(0, 0, 0, current).name == "minecraft:stone");
    composite.reset();
    CHECK(composite.desired(0, 0, 0, current).kind == DesiredBlock::Kind::Any);
    composite.reset();
    current.name = "minecraft:oak_log";
    current.states = {{"pillar_axis", "x"}};
    CHECK(composite.desired(0, 0, 0, current).kind == DesiredBlock::Kind::Any);
}

TEST(settings_profiles_missing_file_preserves_current_values) {
    SettingsGuard guard;
    TempDir tmp;
    std::filesystem::create_directories(tmp.path);
    auto file = tmp.path / "settings.txt";
    Settings::get().allowBreak = false;
    CHECK(saveSettings(file.string()));
    Settings::get().allowBreak = true;
    CHECK(loadSettings(file.string(), true) >= 1);
    CHECK(!Settings::get().allowBreak);
    std::filesystem::remove(file);
    CHECK(loadSettings(file.string(), true) == -1);
    CHECK(!Settings::get().allowBreak);
    const auto* color = findSetting("colorCurrentPath");
    const auto old = settingValueToString(color->ptr);
    bool rejected = false;
    try { parseSettingValue(*color, "99999999999999999,0,0"); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    CHECK(settingValueToString(color->ptr) == old);
}

TEST(click_selection_goal_above_drag_miss_and_cancel) {
    ClickSelection click;
    BlockPos a{1, 2, 3}, b{4, 5, 6};
    click.start();
    CHECK(!click.event(1, true, a));
    auto goal = click.event(1, false, a);
    CHECK(goal && goal->kind == ClickSelection::Result::Goal && goal->first == a);
    CHECK(!click.active());
    click.start();
    click.event(2, true, a);
    goal = click.event(2, false, a);
    CHECK(goal && goal->kind == ClickSelection::Result::Goal && goal->first == a.above());
    click.start();
    click.event(1, true, a);
    CHECK(click.dragStart() == a);
    auto box = click.event(1, false, b);
    CHECK(box && box->kind == ClickSelection::Result::Box && box->first == a && box->second == b);
    click.start();
    click.event(1, true, a);
    auto miss = click.event(1, false, std::nullopt);
    CHECK(miss && miss->kind == ClickSelection::Result::Miss && !click.active());
    click.start();
    click.event(1, true, a);
    click.cancel();
    CHECK(!click.event(1, false, b));
    CHECK(!click.active() && !click.dragStart());
}

TEST(disconnect_only_once_on_arrival_when_enabled) {
    SettingsGuard guard;
    TestWorld world(1);
    world.floor(0, Blocks::get().stone);
    for (bool enabled : {false, true}) {
        Settings::get().disconnectOnArrival = enabled;
        test::TestPlayer player;
        Baritone bot(player, [](const std::string&, bool) {});
        bot.customGoal().setGoalAndPath(std::make_shared<GoalBlock>(0, 1, 0));
        for (int tick = 0; tick < 10; ++tick) bot.tick(world.snapshot());
        CHECK(player.disconnectRequests == (enabled ? 1 : 0));
    }
}

TEST(elytra_allocator_modes_keep_the_same_obstacle_avoiding_path) {
    TestWorld world(1);
    world.fill(0, 0, -5, 0, 12, 5, Blocks::get().stone);
    std::vector<BlockPos> previous;
    for (bool custom : {false, true}) {
        elytra::ElytraPathFinder::Options options;
        options.customAllocator = custom;
        options.clearance = 1;
        elytra::FlightPathResult result;
        {
            elytra::ElytraPathFinder finder(world.snapshot(), options);
            result = finder.calculate({-10, 5, 0}, {10, 5, 0}, 1, 2000);
        }
        CHECK(result.reachesGoal && !result.path.empty());
        if (!previous.empty()) CHECK(previous == result.path);
        previous = result.path;
    }
}

TEST(elytra_collision_debug_records_the_tested_sweep) {
    TestWorld world(1);
    world.fill(0, 1, 2, 0, 6, 2, Blocks::get().stone);
    BlockStateInterface bsi(world.snapshot());
    std::vector<elytra::CollisionSample> samples;
    auto flight = elytra::simulate(bsi, {{0.5, 4, 0.5}, {0, 0, 10}}, {0, 0}, 1, 0, &samples);
    CHECK(!flight);
    CHECK(!samples.empty());
    CHECK(samples.back().collided);
    CHECK(samples.back().position.z < 2.5);
}

TEST(schematic_workspace_preview_is_bounded_and_uses_live_states) {
    const auto& blocks = Blocks::get();
    TestWorld world(1);
    world.set(0, 1, 0, blocks.stone); // correct: hidden
    world.set(2, 1, 0, blocks.dirt);  // wrong: red
    world.set(3, 1, 0, blocks.stone); // clear: orange
    auto source = std::make_shared<StaticSchematic>(BlockPos{5, 1, 1},
        std::vector{DesiredBlock::block("minecraft:stone"), DesiredBlock::air(), DesiredBlock::any()},
        std::vector{0, 0, 0, 1, 2});
    SchematicWorkspace work;
    work.load("test", source, {0, 1, 0});
    using D = SchematicWorkspace::Difference;
    auto preview = work.preview(*world.snapshot(), {0, 1, 0});
    CHECK(preview.size() == 3);
    CHECK(preview[0].pos == (BlockPos{1, 1, 0}) && preview[0].difference == D::Missing);
    CHECK(preview[1].difference == D::Wrong);
    CHECK(preview[2].difference == D::Remove);
    CHECK(work.preview(*world.snapshot(), {0, 1, 0}, 16, 1).size() == 1);
    CHECK(work.preview(*world.snapshot(), {1000, 1, 0}).empty());
    work.move({15, 1, 0});
    preview = work.preview(*world.snapshot(), {15, 1, 0});
    CHECK(preview.size() == 4 && preview[1].difference == D::Unloaded);
    work.setPreview(false);
    CHECK(work.preview(*world.snapshot(), {15, 1, 0}).empty());
}

TEST(schematic_workspace_transforms_materials_layers_and_build_share_placement) {
    SettingsGuard settings;
    auto log = DesiredBlock::block("minecraft:oak_log");
    log.states = {{"pillar_axis", "x"}};
    auto source = std::make_shared<StaticSchematic>(BlockPos{2, 2, 1},
        std::vector{log, DesiredBlock::air(), DesiredBlock::any()}, std::vector{0, 1, 2, 0});
    SchematicWorkspace work;
    work.load("logs", source, {5, 2, 7});
    work.rotate(1);
    work.mirror(2);
    CHECK(work.schematic()->size() == (BlockPos{1, 2, 2}));
    auto material = work.materials();
    CHECK(material.blocks["minecraft:oak_log"] == 2 && material.air == 1 && material.ignored == 1);
    work.layer(std::pair{1, 1});
    material = work.materials();
    CHECK(material.blocks["minecraft:oak_log"] == 1 && material.ignored == 2);
    // Global file transforms must not transform the already previewed placement again.
    Settings::get().buildSchematicRotation = "CLOCKWISE_90";
    Settings::get().schematicOrientationX = true;
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {});
    bot.builder().build(work.name(), work.schematic(), work.origin(), false, false);
    CHECK(bot.builder().bounds() == std::optional(work.bounds()));
    work.layer(std::nullopt);
    CHECK(work.materials().blocks["minecraft:oak_log"] == 2);
    bool rejected = false;
    try { work.layer(std::pair{2, 2}); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected && !work.layers());
    work.clear();
    CHECK(!work.loaded() && !work.schematic());
}

TEST(schematic_invalid_dimensions_and_indices_are_rejected) {
    auto invalid = [](auto make) {
        bool rejected = false;
        try { make(); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected);
    };
    invalid([] { schematicVolume({INT32_MAX, INT32_MAX, INT32_MAX}); });
    invalid([] { schematicVolume({0, 1, 1}); });
    invalid([] { StaticSchematic s({1, 1, 1}, {DesiredBlock::air()}, {99}); });
    invalid([] { StaticSchematic s({2, 1, 1}, {DesiredBlock::air()}, {0}); });
    SchematicWorkspace work;
    work.load("old", std::make_shared<FillSchematic>(BlockPos{2, 1, 1}, DesiredBlock::air()), {0, 0, 0});
    invalid([&] { work.load("bad", nullptr, {}); });
    CHECK(work.name() == "old");
    invalid([&] { work.move({INT32_MAX, 0, 0}); });
    CHECK(work.origin() == (BlockPos{}));
}

TEST(command_arguments_support_quoted_schematic_names_and_windows_paths) {
    CHECK(commandArguments("schematic load \"castle big.schem\" ~1 ~ ~-2") ==
        (std::vector<std::string>{"schematic", "load", "castle big.schem", "~1", "~", "~-2"}));
    CHECK(commandArguments("build \"C:\\My Builds\\house.schem\"")[1] == "C:\\My Builds\\house.schem");
    CHECK(commandArguments("find oak_log[pillar_axis=\"x\"]")[1] == "oak_log[pillar_axis=\"x\"]");
    bool rejected = false;
    try { commandArguments("build \"missing end"); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    CHECK(commandArguments("build \"not finished", true)[1] == "not finished");
}

TEST(nbt_invalid_collection_lengths_are_rejected_before_allocation) {
    for (uint8_t type : {uint8_t{7}, uint8_t{9}, uint8_t{11}, uint8_t{12}}) {
        for (int32_t count : {-1, INT32_MAX}) {
            NbtWriter w;
            w.named(10, ""); w.named(type, "bad");
            if (type == 9) w.u8(10);
            w.i32(count); w.end();
            bool rejected = false;
            try { nbt::parse(w.out, true); } catch (const std::runtime_error&) { rejected = true; }
            CHECK(rejected);
        }
    }
}

TEST(sponge_v3_nested_palette_and_invalid_indices) {
    auto make = [](uint8_t index) {
        NbtWriter w; w.bigEndian = true;
        w.named(10, ""); w.named(10, "Schematic");
        w.named(3, "Version"); w.i32(3);
        w.named(2, "Width"); w.i16(1); w.named(2, "Height"); w.i16(1); w.named(2, "Length"); w.i16(1);
        w.named(10, "Blocks"); w.named(10, "Palette"); w.named(3, "minecraft:stone"); w.i32(0); w.end();
        w.named(7, "Data"); w.i32(1); w.u8(index); w.end(); w.end(); w.end();
        return w.out;
    };
    auto path = writeTemp("baritone_v3.schem", make(0));
    CHECK(loadSchematic(path)->desired(0, 0, 0, "").name == "minecraft:stone");
    writeTemp("baritone_v3.schem", make(2));
    bool rejected = false;
    try { loadSchematic(path); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    std::filesystem::remove(path);
}

StateId auditCrop(bool mature) {
    BlockProps p;
    p.name = "minecraft:wheat";
    p.collides = false; p.pathfindable = true; p.replaceable = true;
    p.growth = mature ? 7 : 1; p.matureGrowth = 7;
    return BlockRegistry::get().add(p);
}

TEST(farm_initial_scan_with_periodic_rescans_disabled) {
    SettingsGuard settings;
    Settings::get().mineGoalUpdateInterval = 0;
    TestWorld world(1); world.set(2, 1, 0, auditCrop(true));
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {});
    bot.tick(world.snapshot()); bot.farm().farm(10, {});
    bool found = false;
    for (int i = 0; i < 100 && !found; ++i) {
        bot.controls() = {};
        bot.farm().onTick(false, true);
        found = bot.controls().target.rotation.has_value();
        if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(found);
}

TEST(builder_respects_disabled_breaking_for_nearby_wrong_blocks) {
    SettingsGuard settings;
    Settings::get().allowBreak = false; Settings::get().allowBreakAnyway.clear();
    TestWorld world(1); world.floor(0, Blocks::get().stone); world.set(2, 1, 0, Blocks::get().dirt);
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    auto schematic = std::make_shared<FillSchematic>(BlockPos{1, 1, 1}, DesiredBlock::air());
    bot.builder().build("clear", schematic, {2, 1, 0});
    for (int i = 0; i < 3; ++i) {
        bot.controls() = {};
        bot.builder().onTick(false, true);
        if (bot.controls().target.rotation) player.facing = *bot.controls().target.rotation;
        CHECK(!bot.controls().input(Input::ClickLeft));
    }
}

TEST(builder_large_completed_scan_yields_before_completion) {
    struct CountingAir : Schematic {
        mutable std::size_t calls = 0;
        BlockPos size() const override { return {64, 64, 64}; }
        DesiredBlock desired(int, int, int, const BlockProps&) const override { ++calls; return DesiredBlock::air(); }
    };
    SettingsGuard settings;
    Settings::get().buildInLayers = false;
    TestWorld world(4);
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    auto source = std::make_shared<CountingAir>();
    bot.builder().build("large already-complete build", source, {0, 1, 0});
    bot.builder().onTick(false, true);
    CHECK(source->calls <= 8192);
    CHECK(bot.builder().isActive());
    for (int i = 0; i < 1024 && bot.builder().isActive(); ++i) bot.builder().onTick(false, true);
    CHECK(!bot.builder().isActive());
}

TEST(builder_direct_placement_respects_disabled_fluid_replacement) {
    SettingsGuard settings;
    Settings::get().allowPlace = true;
    Settings::get().allowPlaceInFluidsSource = false;
    Settings::get().okIfWater = false;
    TestWorld world(1); world.floor(0, Blocks::get().stone); world.set(2, 1, 0, Blocks::get().water);
    test::TestPlayer player;
    player.inventory[0].name = "minecraft:stone";
    player.inventory[0].count = 32;
    player.inventory[0].isBlock = true;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.builder().build("fluid replacement disabled", std::make_shared<FillSchematic>(BlockPos{1, 1, 1}, DesiredBlock::block("minecraft:stone")), {2, 1, 0});
    for (int i = 0; i < 5; ++i) {
        bot.controls() = {};
        bot.builder().onTick(false, true);
        if (bot.controls().target.rotation) player.facing = *bot.controls().target.rotation;
        CHECK(!bot.controls().input(Input::ClickRight));
    }
}

TEST(builder_incremental_layers_and_repeats_complete_with_a_shared_tick_budget) {
    struct CountingAny : Schematic {
        mutable std::size_t calls = 0;
        BlockPos size() const override { return {32, 4, 32}; }
        DesiredBlock desired(int, int, int, const BlockProps&) const override { ++calls; return DesiredBlock::any(); }
    };
    SettingsGuard settings;
    auto& s = Settings::get();
    s.buildInLayers = true; s.layerHeight = 1; s.startAtLayer = 0;
    s.buildRepeat = {0, 4, 0}; s.buildRepeatCount = 3;
    TestWorld world(2);
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    auto source = std::make_shared<CountingAny>();
    bot.builder().build("layered repeats", source, {0, 1, 0});
    bot.builder().onTick(false, true);
    CHECK(bot.builder().isActive());
    CHECK(source->calls <= 8192);
    for (int i = 0; i < 256 && bot.builder().isActive(); ++i) bot.builder().onTick(false, true);
    CHECK(!bot.builder().isActive());
    CHECK(source->calls >= 3 * 32 * 4 * 32);
}

TEST(follow_pickup_filters_range_and_walks_into_items) {
    SettingsGuard settings;
    Settings::get().followTargetMaxDistance = 10;
    TestWorld world(1);
    test::TestPlayer player;
    EntityInfo item; item.isItem = true; item.itemName = "minecraft:diamond"; item.position = {5.5, 1, 0.5};
    EntityInfo far = item; far.position = {50.5, 1, 0.5};
    EntityInfo other = item; other.itemName = "minecraft:dirt"; other.position = {3.5, 1, 0.5};
    player.nearby = {item, far, other};
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.follow().pickup([](const std::string& name) { return name == "minecraft:diamond"; }, "diamonds");
    CHECK(bot.follow().isActive());
    auto command = bot.follow().onTick(false, true);
    CHECK(command && command->goal && command->goal->isInGoal(5, 1, 0));
    CHECK(!command->goal->isInGoal(3, 1, 0) && !command->goal->isInGoal(50, 1, 0));
    bot.cancelEverything(); CHECK(!bot.follow().isActive());
}

TEST(completed_positions_compact_sections_preserve_negative_coordinates) {
    CompletedPositions done;
    for (int x = -16; x < 0; ++x) for (int y = -16; y < 0; ++y) for (int z = -16; z < 0; ++z) done.insert({x, y, z});
    CHECK(done.sectionCount() == 1 && done.contains({-1, -1, -1}));
    CHECK(!done.contains({0, -1, -1}) && !done.contains({-1, 0, -1}));
    done.erase({-1, -1, -1}); CHECK(!done.contains({-1, -1, -1}));
    CHECK(done.contains({-2, -1, -1}));
    done.clear(); CHECK(done.sectionCount() == 0);
}

TEST(builder_place_override_cannot_bypass_permission_or_fluid_flags) {
    SettingsGuard settings;
    TestWorld world(1);
    Settings::get().allowPlace = false;
    CalculationContext disabled(world.snapshot(), {}, true);
    disabled.placeCostOverride = [](int, int, int, const BlockProps&) { return 0.0; };
    CHECK(disabled.costOfPlacingAt(1, 1, 1, BlockRegistry::get().props(kAir)) >= costs::COST_INF);
    Settings::get().allowPlace = true; Settings::get().allowPlaceInFluidsSource = false;
    CalculationContext dry(world.snapshot(), {}, true);
    dry.placeCostOverride = disabled.placeCostOverride;
    CHECK(dry.costOfPlacingAt(1, 1, 1, BlockRegistry::get().props(Blocks::get().water)) >= costs::COST_INF);
}

TEST(mine_permission_restriction_preserves_states_and_ore_allowlist) {
    BlockFilter filter({"oak_log[pillar_axis=x]", "diamond"});
    auto restricted = filter.restrictedTo({"minecraft:oak_log", "minecraft:diamond_ore"});
    BlockProps log; log.name = "minecraft:oak_log"; log.states = {{"pillar_axis", "x"}};
    CHECK(restricted.matches(log));
    log.states[0].second = "y"; CHECK(!restricted.matches(log));
    CHECK(restricted.matchesName("minecraft:diamond_ore"));
    CHECK(!restricted.matchesName("minecraft:deepslate_diamond_ore"));
}

TEST(explore_sparse_allowlist_targets_distant_chunks_without_ring_enumeration) {
    SettingsGuard settings;
    Settings::get().worldExploringChunkOffset = 0;
    TestWorld world(1);
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.explore().setFilter({WorldSnapshot::key(12000, -12000)}, true);
    bot.explore().explore(0, 0);
    auto start = std::chrono::steady_clock::now();
    auto command = bot.explore().onTick(false, true);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(command && command->goal && command->goal->isInGoal(12000 * 16 + 8, 1, -12000 * 16 + 8));
    CHECK(bot.explore().isActive());
    std::printf("      sparse explore allowlist: %.3f ms\n", ms);
}

TEST(cache_background_save_keeps_concurrent_updates_and_zero_search_does_not_load_regions) {
    SettingsGuard settings;
    Settings::get().cachedChunksExpirySeconds = -1;
    TempDir tmp;
    auto cache = makeCache(tmp.path);
    for (int x = 0; x < 64; ++x) {
        auto chunk = std::make_shared<CachedChunk>(x, 0, -64, 384);
        for (std::size_t i = 0; i < chunk->bits().size(); ++i) chunk->bits()[i] = static_cast<uint8_t>(i % 251);
        cache->put(chunk);
    }
    cache->saveAsync();
    auto latest = std::make_shared<CachedChunk>(0, 0, -64, 384);
    latest->setType(1, 1, 1, PathingBlockType::Water);
    cache->put(latest);
    cache->saveAsync(); // does not wait for an earlier writer
    cache->saveNow();  // explicit barrier also persists edits made during the first save
    cache.reset();
    auto loaded = makeCache(tmp.path);
    CHECK(loaded->loadedRegions() == 0);
    CHECK(loaded->locationsOf([](const std::string&) { return true; }, {}, 0, 1).empty());
    CHECK(loaded->loadedRegions() == 0);
    CHECK(loaded->chunk(0, 0)->type(1, 1, 1) == PathingBlockType::Water);
}

TEST(farm_respects_breaking_permission_and_selection_for_drops) {
    SettingsGuard settings;
    Settings::get().mineGoalUpdateInterval = 0;
    Settings::get().allowBreak = false;
    Settings::get().allowBreakAnyway.clear();
    Settings::get().farmUsingSelection = true;
    TestWorld world(1); world.set(2, 1, 0, auditCrop(true));
    test::TestPlayer player;
    EntityInfo item; item.isItem = true; item.itemName = "minecraft:wheat"; item.position = {10.5, 1, 0.5};
    player.nearby = {item};
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.selections().add({{0, 0, 0}, {3, 3, 3}});
    bot.farm().farm(0, {});
    for (int i = 0; i < 100 && bot.farm().isActive(); ++i) {
        bot.controls() = {};
        auto command = bot.farm().onTick(false, true);
        if (bot.controls().target.rotation) player.facing = *bot.controls().target.rotation;
        CHECK(!bot.controls().input(Input::ClickLeft));
        CHECK(!command || !command->goal || !command->goal->isInGoal(10, 1, 0));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(!bot.farm().isActive());
}

TEST(builder_active_scan_is_cancelled_and_restarted_for_new_schematic) {
    SettingsGuard settings;
    Settings::get().buildInLayers = false;
    TestWorld world(4);
    test::TestPlayer player;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.builder().build("large", std::make_shared<FillSchematic>(BlockPos{64, 64, 64}, DesiredBlock::air()), {0, 1, 0});
    bot.builder().onTick(false, true);
    CHECK(bot.builder().isActive());
    bot.cancelEverything(); CHECK(!bot.builder().isActive());
    bot.builder().build("small", std::make_shared<FillSchematic>(BlockPos{1, 1, 1}, DesiredBlock::air()), {0, 1, 0});
    bot.builder().onTick(false, true);
    CHECK(!bot.builder().isActive());
}

TEST(background_schematic_loader_reports_errors_and_discards_cancelled_results) {
    NbtWriter w; w.bigEndian = true;
    w.named(10, "Schematic");
    w.named(2, "Width"); w.i16(1); w.named(2, "Height"); w.i16(1); w.named(2, "Length"); w.i16(1);
    w.named(10, "Palette"); w.named(3, "minecraft:stone"); w.i32(0); w.end();
    w.named(7, "BlockData"); w.i32(1); w.u8(0); w.end();
    const auto path = writeTemp("baritone_background_load.schem", w.out);
    SchematicLoadJob job;
    CHECK(job.start(path));
    CHECK(!job.start(path));
    job.cancel();
    std::optional<SchematicLoadJob::Result> result;
    for (int i = 0; i < 1000 && !result; ++i) {
        result = job.poll();
        if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(result && result->cancelled && result->schematic && result->error.empty());
    CHECK(!job.running());
    std::filesystem::remove(path);
    CHECK(job.start(path));
    result.reset();
    for (int i = 0; i < 1000 && !result; ++i) {
        result = job.poll();
        if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(result && !result->cancelled && !result->schematic && !result->error.empty());
    CHECK(job.start(path)); job.wait(); CHECK(!job.running());
}

TEST(nbt_deterministic_mutations_do_not_crash_or_allocate_invalid_lengths) {
    std::mt19937 rng(9371);
    for (int trial = 0; trial < 2000; ++trial) {
        std::vector<uint8_t> input(rng() % 256);
        for (auto& byte : input) byte = static_cast<uint8_t>(rng());
        if (input.size() >= 3 && trial % 2 == 0) { input[0] = 10; input[1] = input[2] = 0; }
        for (bool little : {false, true}) {
            try { nbt::parse(input, little); } catch (const std::exception&) {}
        }
    }
}

TEST(compressed_schematic_corruption_is_rejected) {
    for (auto bytes : {std::vector<uint8_t>(kGzipDynamic, kGzipDynamic + sizeof kGzipDynamic),
                       std::vector<uint8_t>(kZlibFixed, kZlibFixed + sizeof kZlibFixed)}) {
        bytes.back() ^= 1; // gzip uncompressed size or zlib Adler checksum
        bool rejected = false;
        try { decompressIfNeeded(bytes); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    }
}

TEST(schematic_survey_counts_only_live_differences_and_yields) {
    TestWorld world(1);
    world.set(0, 1, 0, Blocks::get().dirt);
    world.set(2, 1, 0, Blocks::get().dirt);
    world.set(3, 1, 0, Blocks::get().stone);
    std::vector<int> indices(49, -1);
    indices[0] = indices[1] = indices[3] = indices[48] = 0; indices[2] = 1;
    SchematicSurvey survey;
    survey.start(std::make_shared<StaticSchematic>(BlockPos{49, 1, 1},
        std::vector<DesiredBlock>{DesiredBlock::block("minecraft:stone"), DesiredBlock::air()}, indices), {0, 1, 0}, 7);
    CHECK(!survey.step(*world.snapshot(), 2)); CHECK(survey.scanned() == 2);
    while (survey.active()) survey.step(*world.snapshot(), 5);
    auto r = survey.result();
    CHECK(r.matching == 1 && r.wrong == 1 && r.missing == 1 && r.extra == 1 && r.unknown == 1);
    CHECK(r.items.at("minecraft:stone") == 2 && r.problems.size() == 4);
    CHECK(survey.revision() == 7);
}

TEST(material_plan_reserves_exact_items_before_substitutions) {
    test::TestPlayer player;
    player.inventory[0].name = "minecraft:stone"; player.inventory[0].count = 3;
    player.inventory[9].name = "minecraft:cobblestone"; player.inventory[9].count = 5;
    auto plan = planMaterials({{"minecraft:stone", 8}, {"minecraft:cobblestone", 4}}, player, {"stone>cobblestone"});
    CHECK(plan.size() == 2);
    CHECK(plan[0].item == "minecraft:cobblestone" && plan[0].missing == 0);
    CHECK(plan[1].required == 8 && plan[1].available == 4 && plan[1].missing == 4);
}

TEST(material_plan_counts_both_missing_door_halves_once) {
    TestWorld world(1);
    auto lower = DesiredBlock::block("minecraft:oak_door"), upper = lower;
    lower.states = {{"upper_block_bit", "0"}}; upper.states = {{"upper_block_bit", "1"}};
    SchematicSurvey survey;
    survey.start(std::make_shared<StaticSchematic>(BlockPos{1, 2, 1}, std::vector<DesiredBlock>{lower, upper}, std::vector<int>{0, 1}), {0, 1, 0}, 1);
    while (survey.active()) survey.step(*world.snapshot());
    CHECK(survey.result().missing == 2 && survey.result().items.at("minecraft:oak_door") == 1);
}

TEST(mcstructure_export_roundtrip_preserves_states_and_refuses_unknown_or_overwrite) {
    TempDir dir;
    TestWorld world(1);
    BlockProps log = BlockRegistry::get().props(Blocks::get().stone);
    log.name = "minecraft:oak_log"; log.states = {{"pillar_axis", "x"}};
    world.set(0, 1, 0, BlockRegistry::get().add(log));
    auto file = dir.path / "export.mcstructure";
    exportMcStructure(*world.snapshot(), {{0, 1, 0}, {1, 1, 0}}, file);
    auto loaded = loadMcStructure(file.string());
    CHECK((loaded->size() == BlockPos{2, 1, 1}));
    CHECK(blockMatches(loaded->desired(0, 0, 0, BlockProps{}), log, false));
    CHECK(loaded->desired(1, 0, 0, BlockProps{}).kind == DesiredBlock::Kind::Air);
    bool rejected = false;
    try { exportMcStructure(*world.snapshot(), {{0, 1, 0}, {1, 1, 0}}, file); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { exportMcStructure(*world.snapshot(), {{48, 1, 0}, {48, 1, 0}}, dir.path / "unknown.mcstructure"); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected && !std::filesystem::exists(dir.path / "unknown.mcstructure"));
}

TEST(protected_areas_block_crossing_and_override_costs_without_blocking_other_cells) {
    TestWorld world(1);
    auto policy = std::make_shared<ProtectionPolicy>();
    policy->add({"wall", {{2, 0, -1}, {2, 4, 1}}, ProtectionPolicy::Both});
    world.snapshot()->setProtection(policy);
    CalculationContext ctx(world.snapshot(), {}, true);
    ctx.ignoreBreakRestrictions = true;
    ctx.placeCostOverride = [](int, int, int, const BlockProps&) { return 0.0; };
    CHECK(ctx.breakCostMultiplierAt(2, 1, 0, BlockRegistry::get().props(Blocks::get().dirt)) >= costs::COST_INF);
    CHECK(ctx.costOfPlacingAt(2, 1, 0, BlockRegistry::get().props(kAir)) >= costs::COST_INF);
    CHECK(!ctx.permitsSegment({0, 1, 0}, {4, 1, 0}));
    CHECK(ctx.permitsSegment({0, 1, 3}, {4, 1, 3}));
    CHECK(policy->permitsModify({4, 1, 0}));
    auto snapshotPolicy = ctx.protection;
    world.snapshot()->setProtection(std::make_shared<ProtectionPolicy>());
    CHECK(!snapshotPolicy->permitsEntry({2, 1, 0}));
}

TEST(protected_areas_persist_and_navigation_avoids_them) {
    TempDir dir;
    ProtectionPolicy policy;
    policy.add({"protected home", {{2, 1, -1}, {4, 3, 1}}, ProtectionPolicy::Both});
    policy.save(dir.path / "zones.txt");
    auto restored = ProtectionPolicy::load(dir.path / "zones.txt");
    CHECK(restored.zones().size() == 1 && !restored.permitsModify({3, 1, 0}));
    TestWorld world(1); world.floor(0, Blocks::get().stone);
    world.snapshot()->setProtection(std::make_shared<ProtectionPolicy>(restored));
    auto result = findPath(world, {0, 1, 0}, std::make_shared<GoalBlock>(6, 1, 0));
    CHECK(result.type == PathResultType::SuccessToGoal && result.path);
    for (const auto& movement : result.path->movements()) CHECK(restored.permitsSegment(movement.src, movement.dest));
    CHECK(restored.remove("protected home") && restored.permitsEntry({3, 1, 0}));
}

TEST(task_queue_sequences_recovers_paused_and_rejects_world_mismatch) {
    TaskQueue queue; WorldIdentity world{"My world", 0}; queue.bind(world);
    queue.add({"wait", {"3"}}); queue.add({"goto", {"1", "2", "3"}});
    queue.resume(world);
    int started = 0;
    auto start = [&](const Task&) { ++started; return TaskOutcome{}; };
    auto poll = [](const Task& task) { return TaskOutcome{task.elapsedTicks >= 3 ? TaskStatus::Succeeded : TaskStatus::Running, {}}; };
    queue.tick(world, start, poll); queue.tick(world, start, poll);
    CHECK(started == 1 && queue.currentIndex() == 0);
    auto recovered = TaskQueue::decode(queue.encode());
    CHECK(!recovered.running() && recovered.tasks()[0].status == TaskStatus::Pending);
    bool rejected = false;
    try { recovered.resume({"Other world", 0}); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    for (int i = 0; i < 10 && queue.running(); ++i) queue.tick(world, start, poll);
    CHECK(queue.complete() && started == 2);
    recovered.resume(world); recovered.tick({"My world", 1}, start, poll);
    CHECK(!recovered.running());
}

TEST(task_queue_bounds_retries_and_blocks_following_actions_on_failure) {
    TaskQueue queue; WorldIdentity world{"world", 0}; queue.bind(world);
    Task task{"test", {}}; task.retryLimit = 1; queue.add(task); queue.add({"never", {}}); queue.resume(world);
    int started = 0;
    auto fail = [&](const Task&) { ++started; return TaskOutcome{TaskStatus::Failed, "Missing tool"}; };
    queue.tick(world, fail, {}); CHECK(queue.running());
    queue.tick(world, fail, {}); CHECK(!queue.running() && started == 2 && queue.currentIndex() == 0);
    CHECK(queue.tasks()[0].reason == "Missing tool");
    queue.retryCurrent(); CHECK(queue.tasks()[0].status == TaskStatus::Pending);
    bool rejected = false;
    try { TaskQueue::decode("BARITONE_TASKS 1 \"world\" 0 0 129"); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
}

TEST(task_checkpoint_writer_coalesces_and_preserves_final_snapshot) {
    TempDir dir; CheckpointWriter writer; auto file = dir.path / "tasks.txt";
    writer.submit(file, "first"); writer.submit(file, "second"); writer.submit(file, "latest"); writer.wait();
    std::ifstream input(file); std::string value; input >> value; CHECK(value == "latest");
    CHECK(!std::filesystem::exists(file.string() + ".checkpoint.tmp"));
}

TEST(survival_eats_until_full_and_restores_the_original_slot) {
    SettingsGuard settings;
    TestWorld world(1); world.floor(0, Blocks::get().stone);
    test::TestPlayer player; player.hunger = 10; player.selected = 5; player.facing.pitch = -90;
    player.inventory[0].name = "minecraft:bread"; player.inventory[0].count = 8;
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.survival().setEnabled(true); CHECK(bot.survival().isActive());
    bot.survival().onTick(false, true);
    CHECK(player.heldUse && player.selected == 0);
    player.hunger = 20; bot.survival().onTick(false, true);
    CHECK(!player.heldUse && player.selected == 5 && !bot.survival().isActive());
}

TEST(survival_retreats_from_threats_and_stops_holding_on_disable) {
    TestWorld world(1); world.floor(0, Blocks::get().stone);
    test::TestPlayer player;
    EntityInfo creeper; creeper.type = "minecraft:creeper"; creeper.position = {2, 1, 0}; player.nearby = {creeper};
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.survival().setEnabled(true);
    auto action = bot.survival().onTick(false, true);
    CHECK(action && action->goal && !action->goal->isInGoal({0, 1, 0}) && action->goal->isInGoal({100, 1, 100}));
    player.heldUse = true; bot.survival().setEnabled(false); CHECK(!player.heldUse);
}

TEST(storage_moves_exact_quantity_only_after_acceptance_and_returns) {
    TestWorld world(1); test::TestPlayer player;
    player.inventory[0].name = "minecraft:stone"; player.inventory[0].count = 5;
    player.externalSlots = {{"container_items", 0, false, {}}};
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.storage().addLocation("supplies", {1, 1, 0}); bot.storage().start("supplies", "stone", 2, false);
    for (int i = 0; i < 12 && bot.storage().isActive(); ++i) bot.storage().onTick(false, true);
    CHECK(bot.storage().succeeded() && !player.containerOpen());
    CHECK(player.inventory[0].count == 3 && player.externalSlots[0].stack.count == 2);
}

TEST(storage_server_refusal_never_claims_success_or_changes_items) {
    TestWorld world(1); test::TestPlayer player; player.rejectTransfer = true;
    HotbarStack stone; stone.name = "minecraft:stone"; stone.count = 8;
    player.externalSlots = {{"container_items", 0, false, stone}};
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.storage().addLocation("supplies", {1, 1, 0}); bot.storage().start("supplies", "stone", 2, true);
    for (int i = 0; i < 12 && bot.storage().isActive(); ++i) bot.storage().onTick(false, true);
    CHECK(!bot.storage().succeeded() && !bot.storage().failure().empty());
    CHECK(player.countItem("minecraft:stone") == 0 && player.externalSlots[0].stack.count == 8);
}

TEST(animal_farming_skips_babies_and_preserves_minimum_adult_population) {
    TestWorld world(1); test::TestPlayer player;
    for (int i = 0; i < 4; ++i) {
        EntityInfo animal; animal.id = i + 1; animal.type = "minecraft:cow"; animal.position = {1, 1, 0};
        animal.baby = i == 3; animal.inLove = false; animal.health = 10; player.nearby.push_back(animal);
    }
    Baritone bot(player, [](const std::string&, bool) {}); bot.tick(world.snapshot());
    bot.animals().start("cow", true, 5, 2, 16);
    for (int i = 0; i < 12 && bot.animals().isActive(); ++i) bot.animals().onTick(false, true);
    CHECK(player.attacks == 1 && !player.nearby.back().dead);
}

TEST(transfer_reconciliation_accepts_independent_native_updates_and_rejects_other_changes) {
    CHECK(transferSideCompatible(false, true, 15, 15, 0));
    CHECK(transferSideCompatible(true, false, 0, 15, 0));
    CHECK(transferSideCompatible(false, true, 15, 0, 15));
    CHECK(transferSideCompatible(true, false, 0, 0, 15));
    CHECK(!transferSideCompatible(false, false, 15, 0, 15));
    CHECK(!transferSideCompatible(false, true, 9, 0, 15));
    CHECK(!transferSideCompatible(true, false, 0, 15, 13));
}

TEST(schematic_catalog_restores_transforms_without_mutating_other_placements) {
    SchematicWorkspace work;
    work.load("test", std::make_shared<FillSchematic>(BlockPos{2, 3, 4}, DesiredBlock::block("minecraft:stone")), {2, 1, 2}, "test.mcstructure");
    work.rotate(1); work.mirror(2); work.layer(std::pair{1, 2});
    SchematicCatalog catalog; catalog.store("first", work);
    work.move({12, 1, 2}); catalog.store("second", work);
    CHECK(catalog.select("first", work) && work.origin().x == 2 && work.rotation() == 1 && work.mirrorMode() == 2);
    CHECK(work.schematic()->size() == BlockPos(4, 3, 2));
    TempDir dir; catalog.save(dir.path / "placements.txt");
    SchematicCatalog restored; restored.load(dir.path / "placements.txt");
    CHECK(!restored.select("second", work));
    restored.entry("second").preset.apply(work);
    CHECK(work.origin().x == 12 && work.layers()->first == 1);
    CHECK(catalog.entry("first").resident->origin().x == 2);
}

TEST(schematic_catalog_preview_has_a_shared_limit_and_deduplicates_cells) {
    TestWorld world(1); SchematicWorkspace work;
    work.load("test", std::make_shared<FillSchematic>(BlockPos{16, 5, 16}, DesiredBlock::block("minecraft:stone")), {0, 1, 0}, "test.mcstructure");
    SchematicCatalog catalog; catalog.store("same", work);
    auto cells = catalog.preview(*world.snapshot(), {3, 3, 3}, work, 17);
    CHECK(cells.size() == 17);
    std::set<BlockPos> positions; for (const auto& cell : cells) positions.insert(cell.pos);
    CHECK(positions.size() == cells.size());
}

TEST(portal_routes_choose_nether_shortcut_and_respect_directed_links) {
    PortalGraph graph;
    graph.add("home",0,{0,64,0}); graph.add("far",0,{8000,64,0});
    graph.add("nether-home",1,{0,64,0}); graph.add("nether-far",1,{1000,64,0});
    graph.link("home","nether-home",false); graph.link("nether-far","far",false);
    auto route = graph.route("home","far");
    CHECK(route.stops.size()==4 && route.cost==1032);
    CHECK(graph.route("far","home").stops.size()==2);
    CHECK(PortalGraph::equivalent({-1,64,-9},0,1)==BlockPos(-1,64,-2));
    TempDir dir; graph.save(dir.path/"portals.txt"); PortalGraph copy; copy.load(dir.path/"portals.txt");
    CHECK(copy.route("home","far").stops==route.stops);
    copy.remove("nether-home"); CHECK(copy.route("home","far").stops.size()==2);
}

TEST(quarry_preserves_a_continuous_supported_ramp_and_detects_boundary_liquids) {
    TestWorld world(1); world.fill(-1,0,-1,8,5,8,Blocks::get().stone);
    auto plan=std::make_shared<QuarryPlan>(Selection{{0,1,0},{6,4,6}},true);
    CHECK(plan->rampFloor(0,0)==3 && plan->rampFloor(1,0)==2 && plan->rampFloor(2,0)==1);
    CHECK(plan->desired(0,3,0,{}).kind==DesiredBlock::Kind::Any);
    CHECK(plan->desired(3,1,3,{}).kind==DesiredBlock::Kind::Air);
    QuarryPreflight survey; survey.start(plan);
    while (!survey.step(*world.snapshot(),29)) {}
    CHECK(survey.ready() && survey.failure().empty() && survey.blocks()>0);
    world.fill(-1,2,2,-1,2,2,Blocks::get().water); survey.start(plan);
    while (!survey.step(*world.snapshot(),29)) {}
    CHECK(!survey.ready() && !survey.failure().empty());
}

TEST(tree_detection_requires_soil_natural_canopy_and_a_complete_selected_tree) {
    TestWorld world(1); world.floor(0,Blocks::get().dirt);
    BlockProps log; log.name="minecraft:oak_log"; log.fullCube=log.canPlaceAgainst=true;
    auto logId=BlockRegistry::get().add(log);
    BlockProps leaves; leaves.name="minecraft:oak_leaves"; leaves.type=BlockType::Leaves; leaves.states={{"persistent_bit","0"}};
    auto leafId=BlockRegistry::get().add(leaves);
    world.fill(0,1,0,0,4,0,logId); world.fill(-1,4,-1,1,5,1,leafId); world.set(0,4,0,logId);
    Selection area{{-2,0,-2},{2,6,2}};
    auto tree=identifyTree(*world.snapshot(),area,{0,1,0});
    CHECK(tree && tree->logs.size()==4 && tree->roots.size()==1 && tree->sapling=="minecraft:oak_sapling");
    CHECK(!identifyTree(*world.snapshot(),Selection{{-2,0,-2},{2,2,2}},{0,1,0}));
    leaves.states={{"persistent_bit","1"}}; leafId=BlockRegistry::get().add(leaves);
    world.fill(-1,4,-1,1,5,1,leafId); world.set(0,4,0,logId);
    CHECK(!identifyTree(*world.snapshot(),area,{0,1,0}));
}

TEST(vehicle_routes_use_wide_corridors_and_refuse_land_or_unloaded_water) {
    TestWorld world(1); world.floor(0,Blocks::get().water);
    auto route=findVehiclePath(*world.snapshot(),{0,1,0},{8,1,0},VehicleKind::Boat);
    CHECK(route.error.empty() && route.points.front()==BlockPos(0,1,0) && route.points.back()==BlockPos(8,1,0));
    world.fill(4,1,-1,4,3,1,Blocks::get().stone);
    route=findVehiclePath(*world.snapshot(),{0,1,0},{8,1,0},VehicleKind::Boat);
    CHECK(route.error.empty());
    for (auto p:route.points) CHECK(vehicleNodeClear(*world.snapshot(),p,VehicleKind::Boat));
    CHECK(!vehicleNodeClear(*world.snapshot(),{4,1,0},VehicleKind::Boat));
    CHECK(!findVehiclePath(*world.snapshot(),{0,1,0},{16,1,0},VehicleKind::Boat).error.empty());
    CHECK(!findVehiclePath(*world.snapshot(),{0,1,0},{8,1,0},VehicleKind::Horse).error.empty());
}

TEST(quarry_path_calculations_cannot_destroy_the_access_ramp) {
    TestWorld world(1); world.fill(-1,0,-1,8,5,8,Blocks::get().stone);
    test::TestPlayer player; Baritone bot(player,[](const std::string&,bool) {}); bot.tick(world.snapshot());
    auto plan=std::make_shared<QuarryPlan>(Selection{{0,1,0},{6,4,6}},false);
    bot.builder().build("quarry",std::make_shared<MaskSchematic>(plan,[](int,int,int) { return true; }),plan->origin(),false,false);
    CalculationContext ctx(world.snapshot(),{},false); bot.builder().customizeContext(ctx);
    CHECK(ctx.breakCostMultiplierAt(0,4,0,BlockRegistry::get().props(Blocks::get().stone))>=costs::COST_INF);
    CHECK(ctx.breakCostMultiplierAt(3,2,3,BlockRegistry::get().props(Blocks::get().stone))<costs::COST_INF);
}

TEST(crafting_waits_for_server_acceptance_and_output_and_stops_on_refusal) {
    TestWorld world(1); test::TestPlayer player;
    player.recipeOptions={{"recipe_items","minecraft:stick",0,4}};
    Baritone bot(player,[](const std::string&,bool) {}); bot.tick(world.snapshot());
    bot.craft().start({1,1,0},"stick",3);
    for (int i=0;i<15;++i) bot.craft().onTick(false,true);
    CHECK(player.craftRequests==1 && bot.craft().isActive() && !bot.craft().succeeded());
    player.inventory[0].name="minecraft:stick"; player.inventory[0].count=4; player.crafting=TransferState::Accepted;
    for (int i=0;i<5 && bot.craft().isActive();++i) bot.craft().onTick(false,true);
    CHECK(bot.craft().succeeded() && !player.containerOpen());
    bot.craft().start({1,1,0},"stick",1);
    for (int i=0;i<8;++i) bot.craft().onTick(false,true);
    player.crafting=TransferState::Rejected; bot.craft().onTick(false,true);
    CHECK(!bot.craft().isActive() && !bot.craft().succeeded() && !bot.craft().failure().empty());
    CHECK(player.countItem("minecraft:stick")==4);
}

TEST(smelting_collects_confirmed_output_and_preserves_preexisting_furnace_items) {
    TestWorld world(1); test::TestPlayer player;
    player.inventory[0].name="minecraft:raw_iron"; player.inventory[0].count=8;
    player.inventory[1].name="minecraft:coal"; player.inventory[1].count=1;
    player.externalSlots={{"furnace_input",0,false,{},25},{"furnace_fuel",0,false,{},24},{"furnace_output",0,false,{},26}};
    Baritone bot(player,[](const std::string&,bool) {}); bot.tick(world.snapshot());
    bot.smelt().start({1,1,0},"raw_iron","iron_ingot",8,"coal",1);
    for (int i=0;i<8;++i) bot.smelt().onTick(false,true);
    CHECK(!bot.smelt().succeeded() && player.externalSlots[0].stack.count==8);
    player.externalSlots[0].stack.count=0; player.externalSlots[1].stack.count=0;
    player.externalSlots[2].stack.name="minecraft:iron_ingot"; player.externalSlots[2].stack.count=8;
    for (int i=0;i<8 && bot.smelt().isActive();++i) bot.smelt().onTick(false,true);
    CHECK(bot.smelt().succeeded() && player.countItem("minecraft:iron_ingot")==8);
    CHECK(transferTargetReceiptValid(24,1,0) && !transferTargetReceiptValid(7,1,0));
    player.inventory[0].name="minecraft:raw_iron"; player.inventory[0].count=8;
    player.inventory[1].name="minecraft:coal"; player.inventory[1].count=1;
    player.externalSlots[0].stack.name="minecraft:sand"; player.externalSlots[0].stack.count=3;
    bot.smelt().start({1,1,0},"raw_iron","iron_ingot",8,"coal",1);
    for (int i=0;i<4 && bot.smelt().isActive();++i) bot.smelt().onTick(false,true);
    CHECK(!bot.smelt().succeeded() && player.externalSlots[0].stack.count==3 && player.countItem("minecraft:raw_iron")==8);
}

TEST(performance_random_terrain) {
    const Blocks& b = Blocks::get();
    TestWorld w(8);  // 256x256
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> h(0, 4);
    for (int x = -128; x < 128; ++x)
        for (int z = -128; z < 128; ++z) {
            int height = h(rng) == 0 ? 2 : 1;
            w.fill(x, -3, z, x, height - 1, z, b.dirt);
        }
    auto start = std::chrono::steady_clock::now();
    auto r = findPath(w, {-100, 3, -100}, std::make_shared<GoalXZ>(100, 100), true, 4000);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("      random terrain: %s in %.0f ms, %d nodes (%.0f nodes/s)\n",
                r.type == PathResultType::SuccessToGoal ? "goal reached" : "segment", ms,
                r.path ? r.path->numNodesConsidered() : 0, r.path ? r.path->numNodesConsidered() / ms * 1000 : 0);
    CHECK_MSG(r.type == PathResultType::SuccessToGoal, describe(r));
}

}  // namespace

int main(int argc, char** argv) {
    std::string filter;
    if (argc == 3 && std::string_view(argv[1]) == "--filter") filter = argv[2];
    else if (argc != 1) { std::printf("Usage: baritone_tests [--filter substring]\n"); return 2; }
    Blocks::get();
    int failed = 0;
    std::size_t executed = 0;
    for (const TestCase& t : tests()) {
        if (!filter.empty() && std::string_view(t.name).find(filter) == std::string_view::npos) continue;
        ++executed;
        try {
            t.fn();
            std::printf("  PASS  %s\n", t.name);
        } catch (const Failure& f) {
            std::printf("  FAIL  %s\n        %s\n", t.name, f.message.c_str());
            failed++;
        } catch (const std::exception& e) {
            std::printf("  FAIL  %s (exception: %s)\n", t.name, e.what());
            failed++;
        }
    }
    std::printf("\n%zu tests, %d failed\n", executed, failed);
    return executed && failed == 0 ? 0 : 1;
}
