// Offline tests of the pathfinding core. Run: build/bin/RelWithDebInfo/baritone_tests.exe
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "TestWorld.h"
#include "baritone/Settings.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/calc/AStarPathFinder.h"
#include "baritone/pathing/goals/Goal.h"
#include "baritone/cache/CachedWorld.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/elytra/ElytraPathFinder.h"
#include "baritone/elytra/ElytraPhysics.h"
#include "baritone/schematic/Inflate.h"
#include "baritone/schematic/Orientation.h"
#include "baritone/schematic/Schematic.h"
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
    auto r = elytra::ElytraPathFinder(w.snapshot()).calculate({0, 20, 0}, {30, 20, 0}, 2.0, 3000);
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

int main() {
    Blocks::get();
    int failed = 0;
    for (const TestCase& t : tests()) {
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
    std::printf("\n%zu tests, %d failed\n", tests().size(), failed);
    return failed == 0 ? 0 : 1;
}
