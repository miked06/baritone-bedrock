#pragma once
#include "sdk/Game.h"
namespace sdk {
struct RecipeChoice { std::string collection,item; int slot=0,count=0; };
std::vector<RecipeChoice> visibleRecipes(LocalPlayer& player,std::string_view item = {});
std::vector<int> craftRecipeOnce(LocalPlayer& player,const RecipeChoice& recipe);
bool discardInventoryStack(LocalPlayer& player,int slot,int count);
void captureRecipeAction(void* manager,const void* action);
}
