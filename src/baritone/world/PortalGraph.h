#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <vector>
#include "baritone/utils/BlockPos.h"
namespace baritone {
class PortalGraph {
public:
    struct Node { int dimension; BlockPos position; };
    struct Route { std::vector<std::string> stops; double cost = 0; };
    void add(std::string name, int dimension, BlockPos position);
    void remove(const std::string& name);
    void link(const std::string& from, const std::string& to, bool bidirectional);
    void unlink(const std::string& from, const std::string& to);
    const auto& nodes() const { return nodes_; }
    const auto& links() const { return links_; }
    Route route(const std::string& from, const std::string& to) const;
    Route route(int dimension, BlockPos start, const std::string& to) const;
    static BlockPos equivalent(BlockPos position, int fromDimension, int toDimension);
    void save(const std::filesystem::path& file) const;
    void load(const std::filesystem::path& file);
private:
    std::map<std::string, Node> nodes_;
    std::set<std::pair<std::string, std::string>> links_;
    Route find(int dimension, BlockPos start, const std::string& to, const std::string& from) const;
};
}
