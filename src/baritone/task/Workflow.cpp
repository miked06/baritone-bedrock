#include "baritone/task/Workflow.h"
#include <iomanip>
#include <sstream>
namespace baritone {
void Workflow::bind(WorldIdentity id) {
    if (!nodes_.empty() && !(id == identity_)) throw std::invalid_argument("Workflow belongs to another world/dimension");
    identity_ = std::move(id);
}
void Workflow::set(std::size_t index, Node node) {
    if (running_ || index > nodes_.size() || index >= 128 || node.success < -2 || node.success >= 128 || node.failure < -2 || node.failure >= 128 || node.visitLimit < 1 || node.visitLimit > 100)
        throw std::invalid_argument("Edit a paused workflow: nodes 0..127, edges -1 finish/-2 stop or node index, visits 1..100");
    TaskQueue validator; validator.add(node.task);
    node.task.status = TaskStatus::Pending; node.task.elapsedTicks = node.task.attempts = 0; node.task.reason.clear(); node.visits = 0;
    if (index == nodes_.size()) nodes_.push_back(std::move(node)); else nodes_[index] = std::move(node);
    restart();
}
void Workflow::clear() { pause(); nodes_.clear(); restart(); }
void Workflow::restart() {
    if (running_) throw std::invalid_argument("Pause the workflow before restarting");
    current_ = steps_ = 0; complete_ = false; reason_.clear();
    for (auto& node : nodes_) { node.visits = 0; node.task.status = TaskStatus::Pending; node.task.elapsedTicks = 0; node.task.attempts = 0; }
}
void Workflow::pause() { running_ = false; if (current_ < nodes_.size() && nodes_[current_].task.status == TaskStatus::Running) nodes_[current_].task.status = TaskStatus::Pending; }
void Workflow::resume(const WorldIdentity& id) {
    if (!(id == identity_) || nodes_.empty()) throw std::invalid_argument("Workflow must be nonempty and match this world/dimension");
    for (const auto& node : nodes_) for (int edge : {node.success, node.failure})
        if (edge >= 0 && std::size_t(edge) >= nodes_.size()) throw std::invalid_argument("A workflow edge points to a missing node");
    if (complete_ || current_ >= nodes_.size() || steps_ >= 512) throw std::invalid_argument("Restart this completed or exhausted workflow first");
    if (nodes_[current_].task.status != TaskStatus::Pending) throw std::invalid_argument("Restart after resolving the workflow failure");
    running_ = true; reason_.clear();
}
void Workflow::tick(const WorldIdentity& id, const TaskQueue::Start& start, const TaskQueue::Poll& poll) {
    if (!running_) return;
    if (!(id == identity_)) { pause(); reason_ = "World/dimension changed"; return; }
    auto& node = nodes_.at(current_); auto& task = node.task; TaskOutcome result;
    try {
        if (task.status == TaskStatus::Pending) {
            if (node.visits >= node.visitLimit || steps_ >= 512) { pause(); reason_ = "Workflow loop/step limit reached"; return; }
            ++node.visits; ++steps_; ++task.attempts; task.status = TaskStatus::Running; task.elapsedTicks = 0; result = start(task);
        } else if (++task.elapsedTicks >= task.timeoutTicks) result = {TaskStatus::Failed, "Workflow node timeout"};
        else result = poll(task);
    } catch (const std::exception& e) { result = {TaskStatus::Failed, e.what()}; }
    if (result.status == TaskStatus::Pending) result = {TaskStatus::Failed, "Invalid workflow adapter result"};
    task.status = result.status; task.reason = result.reason.substr(0, 4096);
    if (task.status == TaskStatus::Running) return;
    const int edge = task.status == TaskStatus::Succeeded ? node.success : node.failure;
    if (edge == -1) { complete_ = true; running_ = false; reason_ = result.reason; }
    else if (edge == -2) { running_ = false; reason_ = result.reason.empty() ? "Workflow stopped at a terminal branch" : result.reason; }
    else { current_ = std::size_t(edge); nodes_[current_].task.status = TaskStatus::Pending; }
}
std::string Workflow::encode() const {
    std::ostringstream out;
    out << "BARITONE_WORKFLOW 1 " << std::quoted(identity_.name) << ' ' << identity_.dimension << ' ' << current_ << ' ' << steps_ << ' ' << complete_ << ' ' << nodes_.size() << '\n';
    for (const auto& n : nodes_) {
        out << n.success << ' ' << n.failure << ' ' << n.visitLimit << ' ' << n.visits << ' ' << std::quoted(n.task.kind) << ' ' << n.task.timeoutTicks << ' ' << int(n.task.status) << ' ' << n.task.arguments.size();
        for (const auto& arg : n.task.arguments) out << ' ' << std::quoted(arg);
        out << '\n';
    }
    return out.str();
}
Workflow Workflow::decode(const std::string& text) {
    if (text.size() > 2 * 1024 * 1024) throw std::invalid_argument("Workflow file exceeds two MiB");
    std::istringstream in(text); Workflow parsed; std::string magic; int version, complete; std::size_t count;
    if (!(in >> magic >> version >> std::quoted(parsed.identity_.name) >> parsed.identity_.dimension >> parsed.current_ >> parsed.steps_ >> complete >> count) || magic != "BARITONE_WORKFLOW" || version != 1 || count > 128 || parsed.current_ > count || parsed.steps_ > 512 || complete < 0 || complete > 1 || parsed.identity_.name.size() > 1024)
        throw std::invalid_argument("Invalid workflow header");
    parsed.complete_ = complete != 0;
    for (std::size_t i = 0; i < count; ++i) {
        Node n; int state; std::size_t argc;
        if (!(in >> n.success >> n.failure >> n.visitLimit >> n.visits >> std::quoted(n.task.kind) >> n.task.timeoutTicks >> state >> argc) || argc > 32 || state < 0 || state > int(TaskStatus::Failed) || n.visitLimit < 1 || n.visitLimit > 100 || n.visits > n.visitLimit || n.success < -2 || n.failure < -2 || n.success >= int(count) || n.failure >= int(count)) throw std::invalid_argument("Invalid workflow node");
        for (std::size_t a = 0; a < argc; ++a) { std::string arg; if (!(in >> std::quoted(arg))) throw std::invalid_argument("Truncated workflow node"); n.task.arguments.push_back(std::move(arg)); }
        TaskQueue validator; validator.add(n.task); n.task.status = static_cast<TaskStatus>(state);
        if (n.task.status == TaskStatus::Running) n.task.status = TaskStatus::Pending;
        parsed.nodes_.push_back(std::move(n));
    }
    if (in >> std::ws && in.peek() != EOF) throw std::invalid_argument("Trailing workflow data");
    return parsed;
}
} // namespace baritone
