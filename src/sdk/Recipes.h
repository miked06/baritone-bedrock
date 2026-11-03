#pragma once
#include "sdk/Game.h"
namespace sdk {
struct RecipeChoice { std::string collection,item; int slot=0,count=0; };
std::vector<RecipeChoice> visibleRecipes(LocalPlayer& player,std::string_view item = {});
std::vector<int> craftRecipeOnce(LocalPlayer& player,const RecipeChoice& recipe);
bool discardInventoryStack(LocalPlayer& player,int slot,int count);
void captureRecipeAction(void* manager,const void* action);
struct WorkstationPreview { std::string method, collection; const ItemStack* output = nullptr; int levels = -1, materialCost = 0; };
WorkstationPreview workstationPreview(LocalPlayer& player);
std::vector<int> takeWorkstationOutput(LocalPlayer& player, const WorkstationPreview& expected);
}
