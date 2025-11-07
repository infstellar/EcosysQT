// Minimal behavior tree scaffolding for modular AI decisions
// Header-only to avoid build system changes during introduction
#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <cstddef>

namespace bt {

enum class Status { Success, Failure, Running };

struct TickContext {
    void* self{nullptr};
    void* world{nullptr};
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

class BehaviorTree {
    std::shared_ptr<Node> root;
public:
    BehaviorTree() = default;
    explicit BehaviorTree(std::shared_ptr<Node> r) : root(std::move(r)) {}
    void set_root(std::shared_ptr<Node> r) { root = std::move(r); }
    Status tick(TickContext& ctx) { return root ? root->tick(ctx) : Status::Failure; }
    void reset() { if (root) root->reset(); }
};

} // namespace bt