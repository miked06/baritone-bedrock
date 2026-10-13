#include "TestWorld.h"

namespace test {

namespace {

BlockProps solid(std::string name, float hardness, Tool tool = Tool::Pickaxe, bool requiresTool = true) {
    BlockProps p;
    p.name = std::move(name);
    p.collides = true;
    p.fullCube = true;
    p.canPlaceAgainst = true;
    p.hardness = hardness;
    p.bestTool = tool;
    p.requiresTool = requiresTool;
    return p;
}

BlockProps liquid(std::string name, Fluid fluid, uint8_t level) {
    BlockProps p;
    p.name = std::move(name);
    p.type = fluid == Fluid::Water ? BlockType::Water : BlockType::Lava;
    p.collides = false;
    p.height = 0;
    p.fluid = fluid;
    p.fluidLevel = level;
    p.replaceable = true;
    p.hardness = 100;
    p.unbreakable = true;
    return p;
}

}  // namespace

const Blocks& Blocks::get() {
    static const Blocks blocks = [] {
        BlockRegistry& reg = BlockRegistry::get();
        Blocks b;
        b.stone = reg.add(solid("minecraft:stone", 1.5f));
        b.dirt = reg.add(solid("minecraft:dirt", 0.5f, Tool::Shovel, false));
        b.grass = reg.add(solid("minecraft:grass_block", 0.6f, Tool::Shovel, false));
        BlockProps sand = solid("minecraft:sand", 0.5f, Tool::Shovel, false);
        sand.falling = true;
        b.sand = reg.add(sand);
        BlockProps gravel = solid("minecraft:gravel", 0.6f, Tool::Shovel, false);
        gravel.falling = true;
        b.gravel = reg.add(gravel);
        b.water = reg.add(liquid("minecraft:water", Fluid::Water, 0));
        b.flowingWater = reg.add(liquid("minecraft:flowing_water", Fluid::Water, 3));
        b.lava = reg.add(liquid("minecraft:lava", Fluid::Lava, 0));

        BlockProps slab;
        slab.name = "minecraft:stone_slab";
        slab.type = BlockType::Slab;
        slab.bottomSlab = true;
        slab.height = 0.5f;
        slab.hardness = 2;
        slab.bestTool = Tool::Pickaxe;
        slab.requiresTool = true;
        b.bottomSlab = reg.add(slab);

        BlockProps ladder;
        ladder.name = "minecraft:ladder";
        ladder.type = BlockType::Ladder;
        ladder.pathfindable = true;  // thin collision box: Java isPathfindable(LAND) is true
        ladder.height = 1.f;
        ladder.hardness = 0.4f;
        ladder.bestTool = Tool::Axe;
        b.ladder = reg.add(ladder);

        BlockProps glass = solid("minecraft:glass", 0.3f, Tool::None, false);
        glass.type = BlockType::Glass;
        b.glass = reg.add(glass);

        BlockProps bedrock = solid("minecraft:bedrock", -1);
        bedrock.unbreakable = true;
        b.bedrock = reg.add(bedrock);

        BlockProps flower;
        flower.name = "minecraft:poppy";
        flower.collides = false;
        flower.height = 0;
        flower.pathfindable = true;
        flower.replaceable = false;
        b.flower = reg.add(flower);

        BlockProps web;
        web.name = "minecraft:web";
        web.type = BlockType::Cobweb;
        web.collides = false;
        web.hardness = 4;
        web.bestTool = Tool::Sword;
        b.cobweb = reg.add(web);

        BlockProps fence;
        fence.name = "minecraft:oak_fence";
        fence.type = BlockType::Fence;
        fence.height = 1.5f;
        fence.hardness = 2;
        fence.bestTool = Tool::Axe;
        b.fence = reg.add(fence);
        return b;
    }();
    return blocks;
}

TestWorld::TestWorld(int chunkRadius, int minY, int maxY)
    : radius_(chunkRadius), snapshot_(std::make_shared<WorldSnapshot>(0, minY, maxY)) {
    for (int cx = -chunkRadius; cx < chunkRadius; ++cx)
        for (int cz = -chunkRadius; cz < chunkRadius; ++cz)
            snapshot_->put(std::make_shared<ChunkData>(cx, cz, minY, maxY - minY));
}

void TestWorld::fill(int x0, int y0, int z0, int x1, int y1, int z1, StateId id) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) set(x, y, z, id);
}

void TestWorld::floor(int floorY, StateId id) {
    int e = extent();
    fill(-e, floorY, -e, e - 1, floorY, e - 1, id);
}

}  // namespace test
