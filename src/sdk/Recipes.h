#pragma once
#include "sdk/Game.h"
namespace sdk {
struct RecipeChoice { std::string collection,item; int slot=0,count=0; };
struct CraftDispatch { std::vector<int> requests; std::string_view error; };
std::vector<RecipeChoice> visibleRecipes(LocalPlayer& player,std::string_view item = {});
CraftDispatch prepareCraftRecipe(LocalPlayer& player, const RecipeChoice& recipe);
CraftDispatch takeCraftOutput(LocalPlayer& player, std::string_view item);
CraftDispatch storeCraftOutput(LocalPlayer& player, std::string_view item);
bool discardInventoryStack(LocalPlayer& player,int slot,int count);
void captureRecipeAction(void* manager,const void* action);
struct WorkstationPreview { std::string method, collection; const ItemStack* output = nullptr; int levels = -1, materialCost = 0; };
WorkstationPreview workstationPreview(LocalPlayer& player);
std::vector<int> takeWorkstationOutput(LocalPlayer& player, const WorkstationPreview& expected);
}
