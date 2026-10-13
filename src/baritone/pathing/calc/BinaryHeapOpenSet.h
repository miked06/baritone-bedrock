#pragma once
#include <vector>

#include "baritone/pathing/calc/PathNode.h"

namespace baritone {

// Binary min-heap on combinedCost with decrease-key, 1-indexed like Baritone's.
class BinaryHeapOpenSet {
public:
    BinaryHeapOpenSet() { array_.reserve(1024); array_.push_back(nullptr); }

    bool empty() const { return array_.size() == 1; }
    std::size_t size() const { return array_.size() - 1; }

    void insert(PathNode* node) {
        node->heapPosition = static_cast<int>(array_.size());
        array_.push_back(node);
        update(node);
    }

    // The node's cost decreased: move it up.
    void update(PathNode* node) {
        int index = node->heapPosition;
        int parentIndex = index >> 1;
        double cost = node->combinedCost;
        PathNode* parent = array_[parentIndex];
        while (index > 1 && parent->combinedCost > cost) {
            array_[index] = parent;
            array_[parentIndex] = node;
            node->heapPosition = parentIndex;
            parent->heapPosition = index;
            index = parentIndex;
            parentIndex = index >> 1;
            parent = array_[parentIndex];
        }
    }

    PathNode* removeLowest() {
        PathNode* result = array_[1];
        PathNode* val = array_.back();
        array_.pop_back();
        result->heapPosition = -1;
        if (array_.size() == 1) return result;
        array_[1] = val;
        val->heapPosition = 1;
        int size = static_cast<int>(array_.size()) - 1;
        int index = 1;
        int smallerChild = 2;
        double cost = val->combinedCost;
        while (smallerChild <= size) {
            int right = smallerChild + 1;
            PathNode* smallerChildNode = array_[smallerChild];
            double smallerChildCost = smallerChildNode->combinedCost;
            if (right <= size) {
                PathNode* rightNode = array_[right];
                double rightCost = rightNode->combinedCost;
                if (smallerChildCost > rightCost) {
                    smallerChild = right;
                    smallerChildCost = rightCost;
                    smallerChildNode = rightNode;
                }
            }
            if (cost <= smallerChildCost) break;
            array_[index] = smallerChildNode;
            array_[smallerChild] = val;
            val->heapPosition = smallerChild;
            smallerChildNode->heapPosition = index;
            index = smallerChild;
            smallerChild = index << 1;
        }
        return result;
    }

private:
    std::vector<PathNode*> array_;
};

}  // namespace baritone
