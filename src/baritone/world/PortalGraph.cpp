#include "baritone/world/PortalGraph.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
namespace baritone {
namespace {
double distance(BlockPos a, BlockPos b) {
    double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z; return std::sqrt(x*x+y*y+z*z);
}
}
void PortalGraph::add(std::string name, int dimension, BlockPos pos) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || dimension < 0 || dimension > 2 || nodes_.size() >= 256 || nodes_.contains(name)) throw std::invalid_argument("Use a unique portal name, dimension 0..2; at most 256 portals");
    nodes_.emplace(std::move(name), Node{dimension, pos});
}
void PortalGraph::remove(const std::string& name) {
    if (!nodes_.erase(name)) throw std::invalid_argument("Unknown portal");
    std::erase_if(links_, [&](const auto& link) { return link.first == name || link.second == name; });
}
void PortalGraph::link(const std::string& a, const std::string& b, bool both) {
    if (!nodes_.contains(a) || !nodes_.contains(b) || a == b || nodes_.at(a).dimension == nodes_.at(b).dimension) throw std::invalid_argument("Links require two known portals in different dimensions");
    if (links_.size() + (both ? 2 : 1) > 1024) throw std::invalid_argument("At most 1024 portal links");
    links_.emplace(a, b); if (both) links_.emplace(b, a);
}
void PortalGraph::unlink(const std::string& a, const std::string& b) { if (!links_.erase({a,b})) throw std::invalid_argument("Unknown portal link"); }
PortalGraph::Route PortalGraph::route(const std::string& from, const std::string& to) const {
    auto p = nodes_.find(from); if (p == nodes_.end()) throw std::invalid_argument("Unknown starting portal");
    return find(p->second.dimension, p->second.position, to, from);
}
PortalGraph::Route PortalGraph::route(int dimension, BlockPos start, const std::string& to) const { return find(dimension, start, to, {}); }
PortalGraph::Route PortalGraph::find(int dimension, BlockPos start, const std::string& to, const std::string& from) const {
    if (!nodes_.contains(to)) throw std::invalid_argument("Unknown destination portal");
    std::map<std::string, double> costs; std::map<std::string, std::string> previous; std::set<std::string> visited;
    for (const auto& [name, node] : nodes_) costs[name] = name == from ? 0 : from.empty() && node.dimension == dimension ? distance(start, node.position) : std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        std::string next; double cheapest = std::numeric_limits<double>::infinity();
        for (const auto& [name, cost] : costs) if (!visited.contains(name) && cost < cheapest) { cheapest = cost; next = name; }
        if (next.empty()) break;
        if (next == to) {
            Route result; result.cost = cheapest;
            for (auto name = to; !name.empty(); name = previous[name]) result.stops.push_back(name);
            std::ranges::reverse(result.stops); return result;
        }
        visited.insert(next); const auto& a = nodes_.at(next);
        for (const auto& [name, b] : nodes_) {
            double step = a.dimension == b.dimension ? distance(a.position, b.position) : links_.contains({next, name}) ? 16.0 : std::numeric_limits<double>::infinity();
            if (cheapest + step < costs[name]) { costs[name] = cheapest + step; previous[name] = next; }
        }
    }
    throw std::invalid_argument("No route through the registered portal links");
}
BlockPos PortalGraph::equivalent(BlockPos p, int from, int to) {
    if (from == to) return p;
    if (from == 0 && to == 1) return {int(std::floor(double(p.x)/8)), p.y, int(std::floor(double(p.z)/8))};
    if (from != 1 || to != 0) throw std::invalid_argument("Coordinate scaling only applies between Overworld and Nether");
    if (int64_t(p.x)*8 > INT32_MAX || int64_t(p.x)*8 < INT32_MIN || int64_t(p.z)*8 > INT32_MAX || int64_t(p.z)*8 < INT32_MIN) throw std::invalid_argument("Scaled coordinates overflow");
    return {p.x*8, p.y, p.z*8};
}
void PortalGraph::save(const std::filesystem::path& file) const {
    std::filesystem::create_directories(file.parent_path()); auto tmp=file; tmp += ".tmp";
    std::ofstream out(tmp); out << "BARITONE_PORTALS 1\n";
    for (const auto& [name, n] : nodes_) out << "node " << std::quoted(name) << ' ' << n.dimension << ' ' << n.position.x << ' ' << n.position.y << ' ' << n.position.z << '\n';
    for (const auto& [a,b] : links_) out << "link " << std::quoted(a) << ' ' << std::quoted(b) << '\n';
    out.close(); if (!out) throw std::runtime_error("Could not save portal graph"); std::filesystem::rename(tmp,file);
}
void PortalGraph::load(const std::filesystem::path& file) {
    nodes_.clear(); links_.clear(); if (!std::filesystem::exists(file)) return;
    if (std::filesystem::file_size(file) > 512*1024) throw std::invalid_argument("Portal file is too large");
    std::ifstream in(file); std::string magic, kind; int version;
    if (!(in >> magic >> version) || magic != "BARITONE_PORTALS" || version != 1) throw std::invalid_argument("Invalid portal file");
    PortalGraph parsed;
    while (in >> kind) {
        std::string name, other; int dim; BlockPos p;
        if (kind == "node" && (in >> std::quoted(name) >> dim >> p.x >> p.y >> p.z)) parsed.add(name, dim, p);
        else if (kind == "link" && (in >> std::quoted(name) >> std::quoted(other))) parsed.link(name,other,false);
        else throw std::invalid_argument("Invalid portal graph entry");
    }
    *this=std::move(parsed);
}
}
