#include "baritone/task/TaskQueue.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace baritone {
namespace {
void validate(const Task& task) {
    if (task.kind.empty() || task.kind.size() > 32 || task.arguments.size() > 32 || task.timeoutTicks == 0 ||
        task.timeoutTicks > 20ull * 60 * 60 * 24 * 30 || task.retryLimit > 10 || task.reason.size() > 4096)
        throw std::invalid_argument("Invalid task limits");
    for (const auto& argument : task.arguments)
        if (argument.size() > 4096 || argument.find_first_of("\r\n") != std::string::npos) throw std::invalid_argument("Invalid task argument");
    std::size_t total = 0; for (const auto& argument : task.arguments) total += argument.size();
    if (total > 4096) throw std::invalid_argument("Task arguments exceed 4096 bytes");
}
}
std::string taskStatusName(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending: return "pending";
        case TaskStatus::Running: return "running";
        case TaskStatus::Succeeded: return "succeeded";
        case TaskStatus::Blocked: return "blocked";
        case TaskStatus::Failed: return "failed";
    }
    return "invalid";
}
void TaskQueue::bind(WorldIdentity identity) {
    if (!tasks_.empty() && !(identity_ == identity)) throw std::invalid_argument("This queue belongs to another world/dimension");
    identity_ = std::move(identity);
}
void TaskQueue::add(Task task) {
    validate(task);
    if (tasks_.size() >= 128) throw std::invalid_argument("Queue limit is 128 tasks");
    task.status = TaskStatus::Pending; task.elapsedTicks = 0; task.attempts = 0; task.reason.clear();
    tasks_.push_back(std::move(task));
}
void TaskQueue::resume(const WorldIdentity& identity) {
    if (!(identity_ == identity)) throw std::invalid_argument("Queue world/dimension mismatch");
    if (cursor_ < tasks_.size() && (tasks_[cursor_].status == TaskStatus::Blocked || tasks_[cursor_].status == TaskStatus::Failed))
        throw std::invalid_argument("Resolve the task failure, then use #task retry");
    running_ = !complete();
}
void TaskQueue::pause() {
    running_ = false;
    if (cursor_ < tasks_.size() && tasks_[cursor_].status == TaskStatus::Running) {
        // The adapter cancels the physical action. Restart against the live world.
        tasks_[cursor_].status = TaskStatus::Pending;
    }
}
void TaskQueue::clear() { pause(); tasks_.clear(); cursor_ = 0; }
bool TaskQueue::complete() const { return cursor_ == tasks_.size(); }
void TaskQueue::retryCurrent() {
    if (cursor_ >= tasks_.size()) throw std::invalid_argument("No current task");
    auto& task = tasks_[cursor_];
    if (task.status != TaskStatus::Blocked && task.status != TaskStatus::Failed) throw std::invalid_argument("Current task has not failed");
    task.status = TaskStatus::Pending; task.elapsedTicks = 0; task.reason.clear();
}
void TaskQueue::configure(std::size_t index, uint64_t timeoutTicks, unsigned retries) {
    if (index >= tasks_.size() || tasks_[index].status != TaskStatus::Pending) throw std::invalid_argument("Configure a pending task by its zero-based index");
    auto copy = tasks_[index]; copy.timeoutTicks = timeoutTicks; copy.retryLimit = retries; validate(copy); tasks_[index] = std::move(copy);
}
void TaskQueue::tick(const WorldIdentity& identity, const Start& start, const Poll& poll) {
    if (!running_) return;
    if (!(identity == identity_)) { pause(); return; }
    if (complete()) { running_ = false; return; }
    auto& task = tasks_[cursor_];
    TaskOutcome outcome;
    try {
        if (task.status == TaskStatus::Pending) { ++task.attempts; task.status = TaskStatus::Running; outcome = start(task); }
        else if (++task.elapsedTicks >= task.timeoutTicks) outcome = {TaskStatus::Failed, "Task time limit exceeded"};
        else outcome = poll(task);
    } catch (const std::exception& error) { outcome = {TaskStatus::Failed, error.what()}; }
    if (outcome.status == TaskStatus::Pending) outcome = {TaskStatus::Failed, "Task adapter returned an invalid pending outcome"};
    task.status = outcome.status; task.reason = outcome.reason.substr(0, 4096);
    if (task.status == TaskStatus::Succeeded) {
        ++cursor_; if (complete()) running_ = false;
    } else if (task.status == TaskStatus::Failed && task.attempts <= task.retryLimit) {
        task.status = TaskStatus::Pending; task.elapsedTicks = 0;
    } else if (task.status != TaskStatus::Running) running_ = false;
}
std::string TaskQueue::encode() const {
    std::ostringstream out;
    out << "BARITONE_TASKS 1 " << std::quoted(identity_.name) << ' ' << identity_.dimension << ' ' << cursor_ << ' ' << tasks_.size() << '\n';
    for (const auto& task : tasks_) {
        out << std::quoted(task.kind) << ' ' << int(task.status) << ' ' << task.timeoutTicks << ' ' << task.elapsedTicks << ' '
            << task.retryLimit << ' ' << task.attempts << ' ' << std::quoted(task.reason) << ' ' << task.arguments.size();
        for (const auto& arg : task.arguments) out << ' ' << std::quoted(arg);
        out << '\n';
    }
    return out.str();
}
TaskQueue TaskQueue::decode(const std::string& text) {
    if (text.size() > 2 * 1024 * 1024) throw std::invalid_argument("Task checkpoint exceeds two MiB");
    TaskQueue queue; std::istringstream in(text); std::string magic; int version = 0; std::size_t count = 0;
    if (!(in >> magic >> version >> std::quoted(queue.identity_.name) >> queue.identity_.dimension >> queue.cursor_ >> count)
        || magic != "BARITONE_TASKS" || version != 1 || count > 128 || queue.cursor_ > count || queue.identity_.name.size() > 1024)
        throw std::invalid_argument("Invalid task checkpoint header");
    for (std::size_t i = 0; i < count; ++i) {
        Task task; int status = 0; std::size_t arguments = 0;
        if (!(in >> std::quoted(task.kind) >> status >> task.timeoutTicks >> task.elapsedTicks >> task.retryLimit >> task.attempts >> std::quoted(task.reason) >> arguments)
            || status < 0 || status > int(TaskStatus::Failed) || arguments > 32) throw std::invalid_argument("Invalid task checkpoint entry");
        for (std::size_t j = 0; j < arguments; ++j) { std::string arg; if (!(in >> std::quoted(arg))) throw std::invalid_argument("Truncated task arguments"); task.arguments.push_back(std::move(arg)); }
        validate(task); task.status = static_cast<TaskStatus>(status);
        if (task.status == TaskStatus::Running) task.status = TaskStatus::Pending;
        if ((i < queue.cursor_) != (task.status == TaskStatus::Succeeded)) throw std::invalid_argument("Inconsistent task checkpoint progress");
        queue.tasks_.push_back(std::move(task));
    }
    if (in >> std::ws && in.peek() != EOF) throw std::invalid_argument("Trailing task checkpoint data");
    return queue; // Recovery is always paused until explicitly resumed in this world.
}
}
