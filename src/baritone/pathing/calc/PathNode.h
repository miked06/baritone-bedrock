#pragma once
#include <cstdint>

#include "baritone/pathing/ActionCosts.h"
#include "baritone/pathing/goals/Goal.h"

namespace baritone {

// A* node (Baritone's PathNode).
struct PathNode {
    int x = 0, y = 0, z = 0;
    double cost = costs::COST_INF;     // g: ticks from the start
    double estimatedCostToGoal = 0;    // h
    double combinedCost = 0;           // f = g + h
    PathNode* previous = nullptr;
    int heapPosition = -1;  // index in the open set, -1 when not in it

    void init(int nx, int ny, int nz, const Goal& goal) {
        x = nx;
        y = ny;
        z = nz;
        cost = costs::COST_INF;
        estimatedCostToGoal = goal.heuristic(nx, ny, nz);
        combinedCost = 0;
        previous = nullptr;
        heapPosition = -1;
    }

    bool isOpen() const { return heapPosition != -1; }
};

}  // namespace baritone
