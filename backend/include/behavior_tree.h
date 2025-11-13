// Minimal behavior tree scaffolding for modular AI decisions
// Header-only to avoid build system changes during introduction
#pragma once

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
#include <cstddef>
#include <string>
#include <utility>
#include <unordered_map>
// 诊断日志（可选）：用于装饰器的轻量级运行时观测
#include <spdlog/spdlog.h>

namespace bt {

enum class Status { Success, Failure, Running };

struct TickContext {
    void* self{nullptr};
    void* world{nullptr};
    // 行为树黑板：为装饰器与动作共享的键值存储
    struct Blackboard* blackboard{nullptr};
};

// 简易黑板实现：支持整型、浮点与字符串存储
struct Blackboard {
    std::unordered_map<std::string, int> ints;
    std::unordered_map<std::string, double> doubles;
    std::unordered_map<std::string, std::string> strings;
};

class Node {
public:
    virtual ~Node() = default;
    virtual Status tick(TickContext& ctx) = 0;
    virtual void reset() {}
};

class Composite : public Node {
protected:
    std::vector<std::shared_ptr<Node>> children;
public:
    Composite() = default;
    explicit Composite(std::vector<std::shared_ptr<Node>> ch) : children(std::move(ch)) {}
    void add_child(std::shared_ptr<Node> ch) { children.push_back(std::move(ch)); }
};

class Sequence : public Composite {
    std::size_t current{0};
public:
    using Composite::Composite;
    Status tick(TickContext& ctx) override {
        for (std::size_t i = current; i < children.size(); ++i) {
            auto s = children[i]->tick(ctx);
            if (s == Status::Running) {
                current = i;
                return Status::Running;
            }
            if (s == Status::Failure) {
                current = 0;
                return Status::Failure;
            }
        }
        current = 0;
        return Status::Success;
    }
    void reset() override {
        current = 0;
        for (auto& c : children) c->reset();
    }
};

class Selector : public Composite {
    std::size_t current{0};
public:
    using Composite::Composite;
    Status tick(TickContext& ctx) override {
        for (std::size_t i = 0; i < children.size(); ++i) {
            auto s = children[i]->tick(ctx);
            if (s == Status::Success) {
                current = i;
                return Status::Success;
            }
            if (s == Status::Running) {
                current = i;
                return Status::Running;
            }
        }
        current = 0;
        return Status::Failure;
    }
    void reset() override {
        current = 0;
        for (auto& c : children) c->reset();
    }
};

class PrioritySelector : public Composite {
    // Interrupts lower-priority running branch when a higher-priority one activates
    std::size_t current{static_cast<std::size_t>(-1)}; // SIZE_MAX denotes none
public:
    using Composite::Composite;
    Status tick(TickContext& ctx) override {
        for (std::size_t i = 0; i < children.size(); ++i) {
            auto s = children[i]->tick(ctx);
            if (s == Status::Success || s == Status::Running) {
                if (current != i) {
                    if (current != static_cast<std::size_t>(-1)) {
                        children[current]->reset();
                    }
                    current = i;
                }
                return s;
            }
        }
        // none active
        current = static_cast<std::size_t>(-1);
        return Status::Failure;
    }
    void reset() override {
        if (current != static_cast<std::size_t>(-1)) {
            children[current]->reset();
        }
        current = static_cast<std::size_t>(-1);
        for (auto& c : children) c->reset();
    }
};

class Condition : public Node {
    std::function<bool(TickContext&)> predicate;
public:
    explicit Condition(std::function<bool(TickContext&)> pred) : predicate(std::move(pred)) {}
    Status tick(TickContext& ctx) override { return predicate(ctx) ? Status::Success : Status::Failure; }
};

class Action : public Node {
    std::function<Status(TickContext&)> fn;
public:
    explicit Action(std::function<Status(TickContext&)> f) : fn(std::move(f)) {}
    Status tick(TickContext& ctx) override { return fn(ctx); }
};

class Succeeder : public Node {
    std::shared_ptr<Node> child;
public:
    explicit Succeeder(std::shared_ptr<Node> c) : child(std::move(c)) {}
    Status tick(TickContext& ctx) override { (void)child->tick(ctx); return Status::Success; }
    void reset() override { if (child) child->reset(); }
};

class Failer : public Node {
    std::shared_ptr<Node> child;
public:
    explicit Failer(std::shared_ptr<Node> c) : child(std::move(c)) {}
    Status tick(TickContext& ctx) override { (void)child->tick(ctx); return Status::Failure; }
    void reset() override { if (child) child->reset(); }
};

class Inverter : public Node {
    std::shared_ptr<Node> child;
public:
    explicit Inverter(std::shared_ptr<Node> c) : child(std::move(c)) {}
    Status tick(TickContext& ctx) override {
        auto s = child->tick(ctx);
        if (s == Status::Success) return Status::Failure;
        if (s == Status::Failure) return Status::Success;
        return Status::Running;
    }
    void reset() override { if (child) child->reset(); }
};

// 通用装饰器基类
class Decorator : public Node {
protected:
    std::shared_ptr<Node> child;
public:
    Decorator() = default;
    explicit Decorator(std::shared_ptr<Node> c) : child(std::move(c)) {}
    void set_child(std::shared_ptr<Node> c) { child = std::move(c); }
};

// 进度装饰器：在达到 total_ticks 前持续返回 Running；达到后执行子节点并返回其状态。
class ProgressDecorator : public Decorator {
    std::string total_key;
    std::string current_key;
    int default_total_ticks{1};
public:
    ProgressDecorator(std::shared_ptr<Node> c,
                      std::string totalKey,
                      std::string currentKey,
                      int defaultTotalTicks)
        : Decorator(std::move(c)),
          total_key(std::move(totalKey)),
          current_key(std::move(currentKey)),
          default_total_ticks(defaultTotalTicks) {}

    Status tick(TickContext& ctx) override {
        if (!ctx.blackboard) {
            // 无黑板则直接执行子节点
            return child ? child->tick(ctx) : Status::Failure;
        }
        auto& bb = *ctx.blackboard;
        // 初始化 total_ticks
        int total = default_total_ticks;
        if (auto it = bb.ints.find(total_key); it != bb.ints.end() && it->second > 0) {
            total = it->second;
        } else {
            bb.ints[total_key] = default_total_ticks;
        }

        // 推进 current_ticks
        int current = 0;
        if (auto it2 = bb.ints.find(current_key); it2 != bb.ints.end()) {
            current = it2->second;
        }
        if (current < total) {
            int next = current + 1;
            bb.ints[current_key] = next;
            // 进度未完成：返回 Running，不在黑板写入 skip_movement，避免跨 tick 残留导致卡住
            return Status::Running;
        }

        // 进度完成后执行子节点，并重置进度与移动标记
        Status s = child ? child->tick(ctx) : Status::Success;
        bb.ints[current_key] = 0;           // 下次重新计时
        return s;
    }

    void reset() override {
        // 重置仅重置子节点；是否清零进度由具体使用场景决定
        if (child) child->reset();
    }
};

// 循环进度装饰器：在累计到 total_ticks 之前，每 tick 执行子节点并返回 Running；
// 当达到 total_ticks 时，执行子节点并返回 Success，同时将 current_ticks 重置为 0。
// 适用于“游荡”等需要持续执行子动作且希望外层观察到进度的场景。
class ProgressLoopDecorator : public Decorator {
    std::string total_key;
    std::string current_key;
    int default_total_ticks{1};
    // 控制推进逻辑：当为 true 时，仅在子节点返回 Success 时推进度；
    // 为 false 时，每个 tick 都推进度（旧行为，适合游荡等时间驱动场景）。
    bool advance_on_child_success{false};
public:
    ProgressLoopDecorator(std::shared_ptr<Node> c,
                          std::string totalKey,
                          std::string currentKey,
                          int defaultTotalTicks)
        : Decorator(std::move(c)),
          total_key(std::move(totalKey)),
          current_key(std::move(currentKey)),
          default_total_ticks(defaultTotalTicks),
          advance_on_child_success(false) {}

    ProgressLoopDecorator(std::shared_ptr<Node> c,
                          std::string totalKey,
                          std::string currentKey,
                          int defaultTotalTicks,
                          bool advanceOnSuccess)
        : Decorator(std::move(c)),
          total_key(std::move(totalKey)),
          current_key(std::move(currentKey)),
          default_total_ticks(defaultTotalTicks),
          advance_on_child_success(advanceOnSuccess) {}

    Status tick(TickContext& ctx) override {
        if (!ctx.blackboard) {
            // 无黑板则直接执行子节点
            return child ? child->tick(ctx) : Status::Failure;
        }
        auto& bb = *ctx.blackboard;
        // 初始化 total_ticks
        int total = default_total_ticks;
        if (auto it = bb.ints.find(total_key); it != bb.ints.end() && it->second > 0) {
            total = it->second;
        } else {
            bb.ints[total_key] = default_total_ticks;
        }

        // 推进 current_ticks
        int current = 0;
        if (auto it2 = bb.ints.find(current_key); it2 != bb.ints.end()) {
            current = it2->second;
        }

        // 执行子节点并根据结果推进度/返回状态
        Status child_status = child ? child->tick(ctx) : Status::Success;

        if (child_status == Status::Failure) {
            return Status::Failure;
        }

        // 决定是否推进度
        int next = current;
        bool advanced = false;
        if (advance_on_child_success) {
            if (child_status == Status::Success) {
                next = current + 1;
                advanced = true;
            }
        } else {
            next = current + 1;
            advanced = true;
        }

        // 写回进度（若推进）
        if (advanced) {
            bb.ints[current_key] = next;
        } else {
            bb.ints[current_key] = current; // 保持不变
        }

        if (next < total) {
            return Status::Running;
        }

        // 达到总时长：重置进度并返回 Success（不重复调用子节点）
        bb.ints[current_key] = 0;
        return Status::Success;
    }

    void reset() override {
        if (child) child->reset();
    }
};

// Tick-interval装饰器：降低高开销分支的评估频率，同时缓存最近的执行状态
class TickIntervalDecorator : public Decorator {
    int interval;
    std::string counter_key;
    std::string status_key;
    std::string interval_param_key;
    bool force_evaluate{false};
public:
    TickIntervalDecorator(std::shared_ptr<Node> c, int eval_interval, std::string key_prefix, std::string interval_param = {})
                : Decorator(std::move(c)),
                    interval(std::max(0, eval_interval)),
                    counter_key(key_prefix + "_interval_counter"),
                    status_key(key_prefix + "_interval_status"),
                    interval_param_key(std::move(interval_param)) {}

    Status tick(TickContext& ctx) override {
        if (!child) {
            return Status::Failure;
        }
        if (!ctx.blackboard) {
            force_evaluate = false;
            return child->tick(ctx);
        }

        auto& bb = *ctx.blackboard;
        int counter = 0;
        if (!force_evaluate) {
            const auto it = bb.ints.find(counter_key);
            if (it != bb.ints.end()) {
                counter = it->second;
            }
        }

        if (counter > 0) {
            bb.ints[counter_key] = counter - 1;
            const auto it_status = bb.ints.find(status_key);
            const int cached_raw = (it_status != bb.ints.end()) ? it_status->second : static_cast<int>(Status::Failure);
            force_evaluate = false;
            return static_cast<Status>(cached_raw);
        }

        int effective_interval = interval;
        if (!interval_param_key.empty()) {
            const auto it_param = bb.ints.find(interval_param_key);
            if (it_param != bb.ints.end()) {
                effective_interval = std::max(0, it_param->second);
            }
        }

        bb.ints[counter_key] = effective_interval;
        const Status result = child->tick(ctx);
        bb.ints[status_key] = static_cast<int>(result);
        force_evaluate = false;
        return result;
    }

    void reset() override {
        force_evaluate = true;
        if (child) {
            child->reset();
        }
    }
};

class BehaviorTree {
    std::shared_ptr<Node> root;
    Blackboard blackboard_;
public:
    BehaviorTree() = default;
    explicit BehaviorTree(std::shared_ptr<Node> r) : root(std::move(r)) {}
    void set_root(std::shared_ptr<Node> r) { root = std::move(r); }
    Status tick(TickContext& ctx) { return root ? root->tick(ctx) : Status::Failure; }
    void reset() { if (root) root->reset(); }
    Blackboard& blackboard() { return blackboard_; }
};

} // namespace bt