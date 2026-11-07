#include "baritone/production/RecipePlanner.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace baritone {
namespace {
std::string itemId(std::string value) {
    if (value.find(':') == std::string::npos) value = "minecraft:" + value;
    if (value.empty() || value.size() > 128 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_:. -/") != std::string::npos ||
        value.find_first_of(" ./") != std::string::npos || std::ranges::count(value, ':') != 1 || value.front() == ':' || value.back() == ':')
        throw std::invalid_argument("Expected a namespaced item identifier");
    return value;
}
struct PlanningLimit : std::runtime_error { PlanningLimit() : std::runtime_error("Recipe planning exceeded its bounded search budget") {} };
int totalMissing(const ProductionPlan& plan) {
    int64_t sum = 0;
    for (const auto& [item, count] : plan.missing) sum += count;
    if (sum > 1000000) throw PlanningLimit();
    return int(sum);
}
}
void RecipeBook::set(ProductionRecipe recipe) {
    recipe.output = itemId(std::move(recipe.output));
    if ((!recipes_.contains(recipe.output) && recipes_.size() >= 2048) || recipe.count < 1 || recipe.count > 64 ||
        recipe.ingredients.empty() || recipe.ingredients.size() > 9)
        throw std::invalid_argument("Recipes: at most 2048 outputs, 1..64 output items and 1..9 ingredient groups");
    for (auto& group : recipe.ingredients) {
        if (group.count < 1 || group.count > 64 || group.alternatives.empty() || group.alternatives.size() > 16)
            throw std::invalid_argument("Ingredient groups need 1..16 alternatives and quantity 1..64");
        std::set<std::string> seen;
        for (auto& item : group.alternatives) {
            item = itemId(std::move(item));
            if (!seen.insert(item).second) throw std::invalid_argument("Duplicate ingredient alternative");
        }
    }
    recipes_[recipe.output] = std::move(recipe);
}
void RecipeBook::remove(const std::string& item) {
    if (!recipes_.erase(itemId(item))) throw std::invalid_argument("No recipe description for that output");
}
ProductionPlan RecipeBook::plan(std::string item, int additional, const std::map<std::string, int>& inventory) const {
    if (additional < 1 || additional > 2304) throw std::invalid_argument("Additional production target must be 1..2304");
    ProductionPlan result;
    result.item = itemId(std::move(item)); result.additional = additional;
    if (inventory.size() > 512) throw std::invalid_argument("Inventory description is too large");
    for (const auto& [id, count] : inventory) {
        if (count < 0 || count > 32768) throw std::invalid_argument("Invalid inventory quantity");
        result.remaining[itemId(id)] += count;
    }
    // The requested output is additional: existing output cannot satisfy it.
    const int existingOutput = result.remaining[result.item];
    result.remaining[result.item] = 0;
    unsigned work = 0;
    std::set<std::string> visiting;
    std::function<void(ProductionPlan&, const std::string&, int)> obtain;
    obtain = [&](ProductionPlan& state, const std::string& id, int needed) {
        if (++work > 4096 || visiting.size() >= 32 || needed < 0 || needed > 1000000) throw PlanningLimit();
        const int available = std::min(needed, state.remaining[id]);
        state.remaining[id] -= available; needed -= available;
        if (!needed) return;
        const auto recipe = recipes_.find(id);
        if (recipe == recipes_.end()) {
            state.missing[id] += needed; (void)totalMissing(state); return;
        }
        if (!visiting.insert(id).second) throw std::invalid_argument("Recipe ingredient cycle involving " + id);
        struct Remove { std::set<std::string>& set; const std::string& key; ~Remove() { set.erase(key); } } remove{visiting, id};
        const auto& definition = recipe->second;
        const int batches = (needed + definition.count - 1) / definition.count;
        for (const auto& group : definition.ingredients) {
            std::optional<ProductionPlan> best;
            for (const auto& candidate : group.alternatives) {
                auto trial = state;
                try { obtain(trial, candidate, group.count * batches); }
                catch (const PlanningLimit&) { throw; }
                catch (const std::invalid_argument&) { continue; }
                if (!best || std::tuple{totalMissing(trial), trial.steps.size()} < std::tuple{totalMissing(*best), best->steps.size()})
                    best = std::move(trial);
            }
            if (!best) throw std::invalid_argument("No acyclic ingredient alternative for " + id);
            state = std::move(*best);
        }
        if (state.steps.size() >= 512 || batches * definition.count > 2304) throw PlanningLimit();
        state.steps.push_back({id, batches * definition.count, batches});
        state.remaining[id] += batches * definition.count - needed;
    };
    obtain(result, result.item, additional);
    result.remaining[result.item] += existingOutput + additional;
    return result;
}
void RecipeBook::save(const std::filesystem::path& file) const {
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto temporary = file; temporary += ".tmp";
    std::ofstream out(temporary); out << "BARITONE_RECIPES 1\n";
    for (const auto& [id, recipe] : recipes_) {
        out << std::quoted(id) << ' ' << recipe.count << ' ' << recipe.ingredients.size();
        for (const auto& ingredient : recipe.ingredients) {
            out << ' ' << ingredient.count << ' ' << ingredient.alternatives.size();
            for (const auto& candidate : ingredient.alternatives) out << ' ' << std::quoted(candidate);
        }
        out << '\n';
    }
    out.close(); if (!out) throw std::runtime_error("Cannot write recipe descriptions");
    if(std::filesystem::file_size(temporary)>4*1024*1024){std::filesystem::remove(temporary);throw std::invalid_argument("Recipe file exceeds four MiB");}
    std::filesystem::rename(temporary, file);
}
void RecipeBook::load(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > 4 * 1024 * 1024) throw std::invalid_argument("Recipe file exceeds four MiB");
    std::ifstream input(file); std::string magic; int version = 0;
    if (!(input >> magic >> version) || magic != "BARITONE_RECIPES" || version != 1) throw std::invalid_argument("Invalid recipe file header");
    RecipeBook parsed;
    while (input >> std::ws && input.peek() != EOF) {
        ProductionRecipe recipe; int groups = 0;
        if (!(input >> std::quoted(recipe.output) >> recipe.count >> groups) || groups < 1 || groups > 9 || parsed.recipes_.contains(recipe.output))
            throw std::invalid_argument("Invalid recipe definition");
        recipe.output = itemId(std::move(recipe.output));
        if (parsed.recipes_.contains(recipe.output)) throw std::invalid_argument("Duplicate recipe output");
        for (int i = 0; i < groups; ++i) {
            RecipeIngredient ingredient; int alternatives = 0;
            if (!(input >> ingredient.count >> alternatives) || alternatives < 1 || alternatives > 16) throw std::invalid_argument("Invalid recipe ingredient");
            for (int j = 0; j < alternatives; ++j) {
                std::string id; if (!(input >> std::quoted(id))) throw std::invalid_argument("Truncated recipe ingredient");
                ingredient.alternatives.push_back(std::move(id));
            }
            recipe.ingredients.push_back(std::move(ingredient));
        }
        parsed.set(std::move(recipe));
    }
    recipes_ = std::move(parsed.recipes_);
}
RecipeBook RecipeBook::vanillaBasics() {
    RecipeBook result;
    std::vector<std::string> planks;
    for (const std::string wood : {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak"}) {
        const auto plank = wood + "_planks";
        result.set({plank, 4, {{{wood + "_log"}, 1}}}); planks.push_back(plank);
    }
    result.set({"stick", 4, {{planks, 2}}});
    result.set({"crafting_table", 1, {{planks, 4}}});
    result.set({"chest", 1, {{planks, 8}}});
    result.set({"torch", 4, {{{"coal", "charcoal"}, 1}, {{"stick"}, 1}}});
    result.set({"furnace", 1, {{{"cobblestone", "cobbled_deepslate", "blackstone"}, 8}}});
    result.set({"wooden_pickaxe", 1, {{planks, 3}, {{"stick"}, 2}}});
    result.set({"stone_pickaxe", 1, {{{"cobblestone", "cobbled_deepslate", "blackstone"}, 3}, {{"stick"}, 2}}});
    result.set({"iron_pickaxe", 1, {{{"iron_ingot"}, 3}, {{"stick"}, 2}}});
    result.set({"diamond_pickaxe", 1, {{{"diamond"}, 3}, {{"stick"}, 2}}});
    return result;
}
}
