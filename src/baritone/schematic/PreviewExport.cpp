#include "baritone/schematic/PreviewExport.h"
#include <format>
namespace baritone {
namespace {
void string(std::ostream& out, std::string_view value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32 || c == '<' || c == '>' || c == '&') out << std::format("\\u{:04x}", c);
        else out << c;
    }
    out << '"';
}
}
void writePreviewJson(std::ostream& out, const Schematic& schematic, BlockPos origin, std::string_view name) {
    const auto size = schematic.size();
    if (schematicVolume(size) > 262144) throw std::invalid_argument("Textured preview export supports at most 262144 cells; select a smaller placement or layer range");
    out << "{\"format\":1,\"name\":"; string(out,name);
    out << std::format(",\"origin\":[{},{},{}],\"size\":[{},{},{}],\"blocks\":[",origin.x,origin.y,origin.z,size.x,size.y,size.z);
    unsigned count = 0;
    for (int x=0;x<size.x;++x) for(int y=0;y<size.y;++y) for(int z=0;z<size.z;++z) {
        const auto desired = schematic.desired(x,y,z,BlockProps{});
        if (desired.kind != DesiredBlock::Kind::Block) continue;
        if (++count > 65536) throw std::invalid_argument("Textured preview export limit is 65536 non-air blocks; restrict the layer range");
        if (count > 1) out << ',';
        out << std::format("{{\"p\":[{},{},{}],\"name\":",x,y,z); string(out,desired.name); out << ",\"states\":{";
        for (std::size_t i=0;i<desired.states.size();++i) { if(i) out << ','; string(out,desired.states[i].first); out << ':'; string(out,desired.states[i].second); }
        out << "}}";
    }
    out << "]}"; if (!out) throw std::runtime_error("Could not serialize the textured preview");
}
}
