#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone {

// What a schematic wants at a position.
struct DesiredBlock {
    enum class Kind { Any, Air, Block } kind = Kind::Any;
    std::string name;  // "minecraft:stone" for Kind::Block
    // Bedrock block states; the builder reproduces the orientation ones (see Orientation.h)
    std::vector<std::pair<std::string, std::string>> states;

    static DesiredBlock any() { return {}; }
    static DesiredBlock air() { return {Kind::Air, {}, {}}; }
    static DesiredBlock block(std::string n) { return {Kind::Block, std::move(n), {}}; }
};

// A structure to build (Baritone's ISchematic). Coordinates are relative to its origin.
class Schematic {
public:
    virtual ~Schematic() = default;
    virtual BlockPos size() const = 0;
    // `current` is the block in the world now (used by #sel replace).
    virtual DesiredBlock desired(int x, int y, int z, const std::string& current) const = 0;
    bool inBounds(int x, int y, int z) const {
        BlockPos s = size();
        return x >= 0 && y >= 0 && z >= 0 && x < s.x && y < s.y && z < s.z;
    }
};

using SchematicPtr = std::shared_ptr<const Schematic>;

// #sel set/fill, #sel cleararea: the whole box is one block (or air).
class FillSchematic : public Schematic {
public:
    FillSchematic(BlockPos size, DesiredBlock block) : size_(size), block_(std::move(block)) {}
    BlockPos size() const override { return size_; }
    DesiredBlock desired(int, int, int, const std::string&) const override { return block_; }

protected:
    BlockPos size_;
    DesiredBlock block_;
};

// #sel walls: only the four vertical sides.
class WallsSchematic : public FillSchematic {
public:
    using FillSchematic::FillSchematic;
    DesiredBlock desired(int x, int, int z, const std::string&) const override {
        bool side = x == 0 || z == 0 || x == size_.x - 1 || z == size_.z - 1;
        return side ? block_ : DesiredBlock::any();
    }
};

// #sel shell: the six faces.
class ShellSchematic : public FillSchematic {
public:
    using FillSchematic::FillSchematic;
    DesiredBlock desired(int x, int y, int z, const std::string&) const override {
        bool face = x == 0 || y == 0 || z == 0 || x == size_.x - 1 || y == size_.y - 1 || z == size_.z - 1;
        return face ? block_ : DesiredBlock::any();
    }
};

// #sel replace <from...> <to>
class ReplaceSchematic : public Schematic {
public:
    ReplaceSchematic(BlockPos size, std::unordered_set<std::string> from, DesiredBlock to)
        : size_(size), from_(std::move(from)), to_(std::move(to)) {}
    BlockPos size() const override { return size_; }
    DesiredBlock desired(int, int, int, const std::string& current) const override {
        return from_.contains(current) ? to_ : DesiredBlock::any();
    }

private:
    BlockPos size_;
    std::unordered_set<std::string> from_;
    DesiredBlock to_;
};

// Several schematics placed in one box (Baritone's CompositeSchematic): #sel over many
// selections. The first part containing a position decides.
class CompositeSchematic : public Schematic {
public:
    struct Part {
        BlockPos offset;
        SchematicPtr schematic;
    };
    CompositeSchematic(BlockPos size, std::vector<Part> parts) : size_(size), parts_(std::move(parts)) {}
    BlockPos size() const override { return size_; }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override {
        for (const Part& p : parts_) {
            int rx = x - p.offset.x, ry = y - p.offset.y, rz = z - p.offset.z;
            if (p.schematic->inBounds(rx, ry, rz)) return p.schematic->desired(rx, ry, rz, current);
        }
        return DesiredBlock::any();
    }

private:
    BlockPos size_;
    std::vector<Part> parts_;
};

// mapArtMode: only the highest block of each column and what is above it matter
// (Baritone's MapArtSchematic).
class MapArtSchematic : public Schematic {
public:
    explicit MapArtSchematic(SchematicPtr inner) : inner_(std::move(inner)) {
        BlockPos s = inner_->size();
        heights_.assign(static_cast<std::size_t>(s.x) * s.z, 0);
        for (int x = 0; x < s.x; ++x)
            for (int z = 0; z < s.z; ++z)
                for (int y = s.y - 1; y >= 0; --y)
                    if (inner_->desired(x, y, z, {}).kind == DesiredBlock::Kind::Block) {
                        heights_[static_cast<std::size_t>(x) * s.z + z] = y;
                        break;
                    }
    }
    BlockPos size() const override { return inner_->size(); }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override {
        if (y < heights_[static_cast<std::size_t>(x) * inner_->size().z + z]) return DesiredBlock::any();
        return inner_->desired(x, y, z, current);
    }

private:
    SchematicPtr inner_;
    std::vector<int> heights_;
};

// A schematic read from a file: palette + one index per position.
class StaticSchematic : public Schematic {
public:
    StaticSchematic(BlockPos size, std::vector<DesiredBlock> palette, std::vector<int> indices)
        : size_(size), palette_(std::move(palette)), indices_(std::move(indices)) {}
    BlockPos size() const override { return size_; }
    DesiredBlock desired(int x, int y, int z, const std::string&) const override;

private:
    BlockPos size_;
    std::vector<DesiredBlock> palette_;
    std::vector<int> indices_;  // (x * sizeY + y) * sizeZ + z, -1 = keep what's there
};

// Java MaskSchematic: the inner schematic where the mask is set, "anything" elsewhere.
class MaskSchematic : public Schematic {
public:
    using Mask = std::function<bool(int x, int y, int z)>;
    MaskSchematic(SchematicPtr inner, Mask mask) : inner_(std::move(inner)), mask_(std::move(mask)) {}
    BlockPos size() const override { return inner_->size(); }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override {
        return mask_(x, y, z) ? inner_->desired(x, y, z, current) : DesiredBlock::any();
    }

private:
    SchematicPtr inner_;
    Mask mask_;
};

// Java RotatedSchematic / MirroredSchematic (buildSchematicRotation, buildSchematicMirror):
// positions and the orientation states of the blocks (pillar_axis, directions) are turned.
// rotation: number of clockwise quarter turns (0-3). mirror: 0 none, 1 LEFT_RIGHT (z), 2 FRONT_BACK (x).
class RotatedSchematic : public Schematic {
public:
    RotatedSchematic(SchematicPtr inner, int quarterTurns) : inner_(std::move(inner)), turns_(((quarterTurns % 4) + 4) % 4) {}
    BlockPos size() const override {
        BlockPos s = inner_->size();
        return turns_ % 2 ? BlockPos{s.z, s.y, s.x} : s;
    }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override;

private:
    SchematicPtr inner_;
    int turns_;
};

class MirroredSchematic : public Schematic {
public:
    MirroredSchematic(SchematicPtr inner, int mirror) : inner_(std::move(inner)), mirror_(mirror) {}
    BlockPos size() const override { return inner_->size(); }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override;

private:
    SchematicPtr inner_;
    int mirror_;
};

// The orientation states of a block turned clockwise / mirrored (Java BlockState.rotate/mirror).
DesiredBlock rotateBlock(DesiredBlock block, int quarterTurns);
DesiredBlock mirrorBlock(DesiredBlock block, int mirror);

// Java SphereMask / CylinderMask (#sel sphere, hsphere, cylinder, hcylinder). axis: 'x', 'y', 'z'.
MaskSchematic::Mask sphereMask(BlockPos size, bool filled);
MaskSchematic::Mask cylinderMask(BlockPos size, bool filled, char axis);

// Bedrock .mcstructure (from structure blocks / exported). Throws std::runtime_error.
SchematicPtr loadMcStructure(const std::string& path);

// Any supported file by extension: .mcstructure, .schem (Sponge v1-3), .litematic, .schematic
// (MCEdit, numeric ids from before 1.13).
// Java block states are converted to Bedrock names (properties dropped).
SchematicPtr loadSchematic(const std::string& path);
DesiredBlock legacyBlock(int id, int data);  // pre-1.13 numeric id + metadata
DesiredBlock javaToBedrock(std::string_view state);  // "minecraft:oak_stairs[facing=north,half=top]"
DesiredBlock javaToBedrock(std::string name, const std::map<std::string, std::string>& properties);

}  // namespace baritone
