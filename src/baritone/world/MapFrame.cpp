#include "baritone/world/MapFrame.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace baritone {
namespace {
bool valid(BlockPos p){return p.x>=-30000000 && p.x<=30000000 && p.z>=-30000000 && p.z<=30000000 && p.y>=-2048 && p.y<=2048;}
}
MapFrame surveyMap(const WorldSnapshot& world,WorldIdentity identity,BlockPos player) {
    MapFrame frame;frame.identity=std::move(identity);frame.player=player;
    const int cx=player.x>>4,cz=player.z>>4;frame.chunks.reserve(MapFrame::side*MapFrame::side);
    for(int z=-MapFrame::radius;z<=MapFrame::radius;++z)for(int x=-MapFrame::radius;x<=MapFrame::radius;++x)
        frame.chunks+=world.isLoaded(cx+x,cz+z)?'2':world.wasLoaded(cx+x,cz+z)?'1':'0';
    return frame;
}
std::string MapFrame::encode() const {
    if(identity.name.size()>1024 || identity.name.find_first_of("\r\n")!=std::string::npos || identity.dimension<0 || identity.dimension>2 || !valid(player) || chunks.size()!=side*side || chunks.find_first_not_of("012")!=std::string::npos || path.size()>128 || markers.size()>64 || zones.size()>16)throw std::invalid_argument("Invalid map frame limits");
    std::ostringstream out;out<<"BARITONE_MAP 1 "<<std::quoted(identity.name)<<' '<<identity.dimension<<' '<<player.x<<' '<<player.y<<' '<<player.z<<' '<<chunks<<' '<<path.size()<<' '<<markers.size()<<' '<<zones.size()<<'\n';
    for(const auto pos:path){if(!valid(pos))throw std::invalid_argument("Invalid map path position");out<<pos.x<<' '<<pos.y<<' '<<pos.z<<'\n';}
    for(const auto& marker:markers){if(!valid(marker.position) || marker.label.size()>48 || marker.label.find_first_of("\r\n")!=std::string::npos || std::string_view("WPSB").find(marker.kind)==std::string_view::npos)throw std::invalid_argument("Invalid map marker");out<<marker.kind<<' '<<std::quoted(marker.label)<<' '<<marker.position.x<<' '<<marker.position.y<<' '<<marker.position.z<<'\n';}
    for(const auto& zone:zones){if(!valid(zone.pos1) || !valid(zone.pos2))throw std::invalid_argument("Invalid map zone");out<<zone.pos1.x<<' '<<zone.pos1.y<<' '<<zone.pos1.z<<' '<<zone.pos2.x<<' '<<zone.pos2.y<<' '<<zone.pos2.z<<'\n';}
    auto text=out.str();if(text.size()>15000)throw std::invalid_argument("Map frame exceeds transport limit");return text;
}
MapFrame MapFrame::decode(const std::string& text) {
    if(text.size()>15000)throw std::invalid_argument("Map frame exceeds transport limit");
    std::istringstream in(text);MapFrame frame;std::string magic;int version=0;std::size_t paths=0,markers=0,zones=0;
    if(!(in>>magic>>version>>std::quoted(frame.identity.name)>>frame.identity.dimension>>frame.player.x>>frame.player.y>>frame.player.z>>frame.chunks>>paths>>markers>>zones) || magic!="BARITONE_MAP" || version!=1 || paths>128 || markers>64 || zones>16)throw std::invalid_argument("Invalid map header");
    for(std::size_t i=0;i<paths;++i){BlockPos pos;if(!(in>>pos.x>>pos.y>>pos.z))throw std::invalid_argument("Truncated map path");frame.path.push_back(pos);}
    for(std::size_t i=0;i<markers;++i){MapMarker marker;if(!(in>>marker.kind>>std::quoted(marker.label)>>marker.position.x>>marker.position.y>>marker.position.z))throw std::invalid_argument("Truncated map marker");frame.markers.push_back(std::move(marker));}
    for(std::size_t i=0;i<zones;++i){Selection zone;if(!(in>>zone.pos1.x>>zone.pos1.y>>zone.pos1.z>>zone.pos2.x>>zone.pos2.y>>zone.pos2.z))throw std::invalid_argument("Truncated map zone");frame.zones.push_back(zone);}
    if(in>>std::ws && in.peek()!=EOF)throw std::invalid_argument("Trailing map data");
    (void)frame.encode();return frame;
}
}
