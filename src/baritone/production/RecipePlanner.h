#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace baritone {
struct RecipeIngredient {
    std::vector<std::string> alternatives;
    int count = 1;
};
struct ProductionRecipe {
    std::string output;
    int count = 1;
    std::vector<RecipeIngredient> ingredients;
};
struct ProductionStep {
    std::string item;
    int amount = 0;
    int batches = 0;
};
struct ProductionPlan {
    std::string item;
    int additional = 0;
    std::vector<ProductionStep> steps;
    std::map<std::string, int> missing;
    std::map<std::string, int> remaining;
    bool ready() const { return missing.empty(); }
};
// Ingredient descriptions never contain native recipe/network IDs. Execution
// must discover a current game recipe and verify its actual output separately.
class RecipeBook {
public:
    void set(ProductionRecipe recipe);
    void remove(const std::string& item);
    const std::map<std::string, ProductionRecipe>& recipes() const { return recipes_; }
    ProductionPlan plan(std::string item, int additional, const std::map<std::string, int>& inventory) const;
    void save(const std::filesystem::path& file) const;
    void load(const std::filesystem::path& file);
    static RecipeBook vanillaBasics();
private:
    std::map<std::string, ProductionRecipe> recipes_;
};
}
