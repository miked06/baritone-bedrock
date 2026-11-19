#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include "baritone/Settings.h"
#include "baritone/schematic/SchematicWorkspace.h"

namespace platform {

struct GhostTexturePack {
    static constexpr int tileSize = 8;
    static constexpr std::size_t colorCount = 48;
    static constexpr uint8_t transparent = 255;
    using Tile = std::array<uint8_t, tileSize * tileSize>;
    // Down, up, north, south, west, east.
    using Block = std::array<std::shared_ptr<const Tile>, 6>;
    std::map<std::string, Block, std::less<>> blocks;
    std::array<baritone::Color, colorCount> colors{};
    std::size_t imageCount = 0;
};

GhostTexturePack loadGhostTexturePack(const std::filesystem::path& folder,
                                      const baritone::SchematicWorkspace::Materials& materials);

}  // namespace platform
