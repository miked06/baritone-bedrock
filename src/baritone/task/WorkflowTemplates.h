#pragma once
#include "baritone/task/Workflow.h"
#include <map>
namespace baritone {
struct WorkflowTemplateInfo { std::string name, arguments, description; };
const std::vector<WorkflowTemplateInfo>& workflowTemplates();
// Inventory-sensitive targets are captured when the template is instantiated.
// Loading a template always produces a paused graph, never an executing command.
Workflow instantiateWorkflow(const std::string& name, const std::vector<std::string>& arguments,
                             WorldIdentity identity, const std::map<std::string,int>& inventory);
}
