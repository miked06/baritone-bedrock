#include "baritone/task/WorkflowTemplates.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/process/FarmProcess.h"
#include <charconv>
#include <map>
#include <stdexcept>

namespace baritone {
const std::vector<WorkflowTemplateInfo>& workflowTemplates() {
    static const std::vector<WorkflowTemplateInfo> entries={
        {"prepare-build","file x y z source material target","Restock a material, build a file and verify its placement"},
        {"mine-store","block item additional destination","Gather an additional quantity and deposit it, preserving the starting stock"},
        {"produce-store","station item additional supply destination","Gather/craft additional output and deliver it, preserving the starting stock"},
        {"equip","profile [source]","Fill and equip a saved loadout from registered storage"}
        ,{"harvest-store","crop additional destination [radius]","Harvest one crop and deposit its new output, preserving starting stock"}
    };
    return entries;
}
namespace {
int number(const std::string& value,int low,int high) {
    int result=0; const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if (parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<low || result>high) throw std::invalid_argument("Template quantity/coordinate is outside its supported range");
    return result;
}
}
Workflow instantiateWorkflow(const std::string& name,const std::vector<std::string>& args,WorldIdentity identity,const std::map<std::string,int>& inventory) {
    Workflow flow; flow.bind(std::move(identity));
    std::vector<Task> tasks;
    auto add=[&](std::string kind,std::vector<std::string> arguments) {
        Task task; task.kind=std::move(kind); task.arguments=std::move(arguments); task.timeoutTicks=20*60*60; tasks.push_back(std::move(task));
    };
    auto have=[&](const std::string& item) { const auto at=inventory.find(BlockFilter::normalize(item)); return at==inventory.end()?0:at->second; };
    if (name=="prepare-build" && args.size()==7) {
        for (int i=1;i<=3;++i) number(args[i],-30000000,30000000);
        number(args[6],1,2304);
        add("restock",{args[5],args[6],args[4]}); add("placementbuild",{args[0],args[1],args[2],args[3]}); add("verify",{});
    } else if (name=="mine-store" && args.size()==4) {
        const int amount=number(args[2],1,2304), baseline=have(args[1]);
        if (baseline<0 || baseline>2304-amount) throw std::invalid_argument("Template output exceeds inventory capacity");
        const BlockFilter blocks({args[0]}); const auto drops=blocks.dropNames();
        if (!drops.contains(BlockFilter::normalize(args[1]))) throw std::invalid_argument("Mining template requires a block with a known matching item drop");
        add("gather",{args[0],args[1],std::to_string(baseline+amount)});
        add("deposit",{args[3],args[1],args[2],std::to_string(baseline)});
    } else if (name=="produce-store" && args.size()==5) {
        const int amount=number(args[2],1,2304), baseline=have(args[1]);
        if (baseline<0 || baseline>2304-amount) throw std::invalid_argument("Template output exceeds inventory capacity");
        add("produce",{args[0],args[1],std::to_string(baseline+amount),args[3]});
        add("deposit",{args[4],args[1],args[2],std::to_string(baseline)});
    } else if(name=="harvest-store" && (args.size()==3 || args.size()==4)) {
        const auto item=FarmProcess::cropDrop(args[0]);const int baseline=have(item),amount=number(args[1],1,2304);
        const int radius=args.size()==4?number(args[3],1,64):16;
        if(baseline<0 || baseline>2304-amount)throw std::invalid_argument("Harvest template exceeds inventory capacity");
        add("harvest",{args[0],std::to_string(baseline+amount),std::to_string(radius)});
        add("deposit",{args[2],item,args[1],std::to_string(baseline)});
    } else if (name=="equip" && (args.size()==1 || args.size()==2)) add("loadout",args);
    else throw std::invalid_argument("Unknown template or wrong arguments; use #workflow templates");
    for (std::size_t i=0;i<tasks.size();++i) {
        Workflow::Node node; node.task=std::move(tasks[i]); node.success=i+1<tasks.size()?int(i+1):-1; node.failure=-2;
        flow.set(i,std::move(node));
    }
    return flow;
}
}
