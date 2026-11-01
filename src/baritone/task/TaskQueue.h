#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace baritone {
struct WorldIdentity {
    std::string name;
    int dimension = 0;
    bool operator==(const WorldIdentity&) const = default;
};
enum class TaskStatus { Pending, Running, Succeeded, Blocked, Failed };
struct TaskOutcome { TaskStatus status = TaskStatus::Running; std::string reason; };
struct Task {
    std::string kind;
    std::vector<std::string> arguments;
    uint64_t timeoutTicks = 12000, elapsedTicks = 0;
    unsigned retryLimit = 0, attempts = 0;
    TaskStatus status = TaskStatus::Pending;
    std::string reason;
};
class TaskQueue {
public:
    using Start = std::function<TaskOutcome(const Task&)>;
    using Poll = std::function<TaskOutcome(const Task&)>;
    void add(Task task);
    void bind(WorldIdentity identity);
    void resume(const WorldIdentity& identity);
    void pause();
    void clear();
    bool running() const { return running_; }
    bool complete() const;
    const WorldIdentity& identity() const { return identity_; }
    const std::vector<Task>& tasks() const { return tasks_; }
    std::size_t currentIndex() const { return cursor_; }
    // One dispatch/poll per tick. Never spins through a long queue in one frame.
    void tick(const WorldIdentity& identity, const Start& start, const Poll& poll);
    void retryCurrent();
    void configure(std::size_t index, uint64_t timeoutTicks, unsigned retries);
    std::string encode() const;
    static TaskQueue decode(const std::string& text);
private:
    WorldIdentity identity_;
    std::vector<Task> tasks_;
    std::size_t cursor_ = 0;
    bool running_ = false;
};
std::string taskStatusName(TaskStatus status);
}
