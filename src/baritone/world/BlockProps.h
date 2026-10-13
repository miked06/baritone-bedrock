#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace baritone {

// Index of a block state in the BlockRegistry. 0 is always air.
using StateId = uint16_t;
inline constexpr StateId kAir = 0;
inline constexpr std::size_t kMaxStates = 1 << 16;

// Blocks that MovementHelper treats specially (Baritone compares against Blocks.X or
// instanceof). Everything else is described by the generic flags below.
enum class BlockType : uint8_t {
    Other,
    Air,
    Water,
    Lava,
    Ladder,
    Vine,
    SoulSand,
    Magma,
    Cactus,
    SweetBerryBush,
    Fire,
    Cobweb,
    EndPortal,
    BubbleColumn,
    Honey,
    PowderSnow,
    Farmland,
    DirtPath,
    Slab,
    Stairs,
    Carpet,
    SnowLayer,
    Door,
    IronDoor,
    FenceGate,
    Trapdoor,
    Fence,
    Wall,
    LilyPad,
    Ice,
    Infested,
    Chest,
    Glass,
    Scaffolding,
    Leaves,
    Cauldron,
    TallPlant,  // tall grass / large fern: replaceable
};

enum class Fluid : uint8_t { None, Water, Lava };
enum class Tool : uint8_t { None, Pickaxe, Axe, Shovel, Hoe, Sword, Shears };

// Everything the pathfinder needs to know about one block state. Filled by the platform
// layer (from the game's block registry) or by the tests.
struct BlockProps {
    std::string name;  // "minecraft:stone"
    // Block states as text ("pillar_axis" = "y", "upside_down_bit" = "1"...), for the builder.
    std::vector<std::pair<std::string, std::string>> states;
    std::string_view state(std::string_view key) const {
        for (const auto& [k, v] : states)
            if (k == key) return v;
        return {};
    }
    BlockType type = BlockType::Other;

    // Collision
    bool collides = true;   // has a collision box at all
    bool fullCube = false;  // collision box is exactly the full block (Block.isShapeFullBlock)
    bool solidTop = false;  // not a full cube, but the top covers the whole block (soul sand, mud, path...)
    float height = 1.f;     // top of the collision box (1.5 for fences/walls)
    bool pathfindable = false;  // Java isPathfindable(LAND): a mob could walk through it

    // Fluids. fluidLevel is Bedrock's liquid_depth (= Java's LEVEL): 0 source, 1-7 flowing,
    // 8+ falling.
    Fluid fluid = Fluid::None;
    uint8_t fluidLevel = 0;

    // Shape details MovementHelper checks
    bool bottomSlab = false;
    bool topSlab = false;
    bool doubleSlab = false;
    bool topHalfStairs = false;
    bool innerStairs = false;  // Bedrock doesn't store the stairs shape: always false
    bool topTrapdoor = false;
    uint8_t snowLayers = 0;
    bool open = false;  // doors, gates, trapdoors
    // Crops: current growth stage and the stage at which they're ready (0 = not a crop).
    uint8_t growth = 0;
    uint8_t matureGrowth = 0;

    // Behaviour
    bool falling = false;      // sand, gravel, concrete powder...
    bool replaceable = false;  // canBeReplaced (air, grass, fluids, snow layer 1...)
    bool canPlaceAgainst = false;
    bool unbreakable = false;  // bedrock, barrier, end portal frame...

    // Mining (Block.destroySpeed / hardness, -1 = unbreakable)
    float hardness = 0.f;
    Tool bestTool = Tool::None;
    bool requiresTool = false;  // drops nothing / slow without the right tool

    bool hasFluid() const { return fluid != Fluid::None; }
    // Java FluidState.getAmount(): 8 for sources and falling liquid, 8 - level when flowing.
    int fluidAmount() const {
        if (fluid == Fluid::None) return 0;
        return (fluidLevel == 0 || fluidLevel >= 8) ? 8 : 8 - fluidLevel;
    }
};

// Registry of every block state the bot has seen. Append-only: entries are written once on
// the game thread before their id is published, then only read (from any thread).
class BlockRegistry {
public:
    static BlockRegistry& get();

    const BlockProps& props(StateId id) const { return states_[id]; }
    StateId add(BlockProps props);  // game thread only
    std::size_t size() const { return count_.load(std::memory_order_acquire); }
    void reset();  // keeps air

private:
    BlockRegistry();
    std::array<BlockProps, kMaxStates> states_;
    std::atomic<std::size_t> count_{1};
};

}  // namespace baritone
