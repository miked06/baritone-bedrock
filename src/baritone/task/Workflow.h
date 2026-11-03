#pragma once
#include "baritone/task/TaskQueue.h"
namespace baritone {
class Workflow {
public:
    struct Node { Task task; int success = -1, failure = -2; unsigned visitLimit = 1, visits = 0; };
    void bind(WorldIdentity identity);
    void set(std::size_t index, Node node);
    void clear();
    void resume(const WorldIdentity& identity);
    void pause();
    void restart();
    void tick(const WorldIdentity& identity, const TaskQueue::Start& start, const TaskQueue::Poll& poll);
    bool running() const { return running_; }
    bool complete() const { return complete_; }
    const auto& nodes() const { return nodes_; }
    std::size_t current() const { return current_; }
    const std::string& reason() const { return reason_; }
    std::string encode() const;
    static Workflow decode(const std::string& text);
private:
    WorldIdentity identity_;
    std::vector<Node> nodes_;
    std::size_t current_ = 0;
    unsigned steps_ = 0;
    bool running_ = false, complete_ = false;
    std::string reason_;
};
}
