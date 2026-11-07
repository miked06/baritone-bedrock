#include "core/Bot.h"
#include "core/DevPipe.h"
#include "baritone/world/MapFrame.h"
#include "platform/WorldTracker.h"
#include <algorithm>
#include <cstdlib>

void Bot::updateMap() {
    try {
    const auto world=platform::WorldTracker::get().snapshot();if(!world)return;
    auto frame=baritone::surveyMap(*world,identity(),baritone_->playerFeet());
    frame.path=baritone_->pathing().currentPathPositions();frame.path.erase(frame.path.begin(),frame.path.begin()+std::min(frame.path.size(),baritone_->pathing().currentPathPosition()));if(frame.path.size()>128)frame.path.resize(128);
    const int cx=frame.player.x>>4,cz=frame.player.z>>4;
    auto add=[&](char kind,std::string name,baritone::BlockPos pos){if(frame.markers.size()<64 && name.find_first_of("\r\n")==std::string::npos && std::abs(int64_t(pos.x>>4)-cx)<=baritone::MapFrame::radius && std::abs(int64_t(pos.z>>4)-cz)<=baritone::MapFrame::radius)frame.markers.push_back({kind,name.substr(0,48),pos});};
    for(const auto& [name,pos]:baritone_->storage().locations())add('S',name,pos);
    for(const auto& [name,node]:portals_.nodes())if(node.dimension==world->dimension())add('P',name,node.position);
    for(const auto& wp:waypoints_.list())if(wp.dimension==world->dimension())add('W',wp.name,wp.pos);
    if(schematics_.loaded())add('B',schematics_.name(),schematics_.origin());
    if(auto policy=world->protection())for(const auto& zone:policy->zones())if(frame.zones.size()<16 && (zone.bounds.max().x>>4)>=cx-baritone::MapFrame::radius && (zone.bounds.min().x>>4)<=cx+baritone::MapFrame::radius && (zone.bounds.max().z>>4)>=cz-baritone::MapFrame::radius && (zone.bounds.min().z>>4)<=cz+baritone::MapFrame::radius)frame.zones.push_back(zone.bounds);
    devpipe::setMap(frame.encode());
    }catch(const std::exception&){devpipe::setMap({});}
}
