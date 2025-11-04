/*
物种数据模型 - Species 基类实现
定义生态系统中的基础物种类
*/

#include "species.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>

// --- Species ---
// 基础物种类
Species::Species(Position pos, double energy, int max_age, double reproduction_energy_cost)
    : position(pos),
    energy(energy),
    max_energy(energy * 4),
    age(0),
    max_age(max_age),
    alive(true),
    reproduction_cooldown(0),
    death_reason(""),
    species_name("Species"),
    reproduction_energy_cost(reproduction_energy_cost),
    pending_spawn_position(std::nullopt) {}

void Species::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    (void)ecosystem_state; // 基类暂不使用共享状态
    (void)rng; // 基类暂不使用随机性
    if (!alive) {
        return;
    }
    if (reproduction_cooldown > 0) {
        reproduction_cooldown -= 1;
    }
    age += 1;
    if (age >= max_age) {
        die("Old age");
    }
}

void Species::apply(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state; // 基类当前无额外应用逻辑
}

bool Species::can_reproduce() const {
    // 检查是否可以繁殖
    return alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
}

std::unique_ptr<Species> Species::reproduce(const EcosystemState& ecosystem_state) {
    // 基础物种不繁殖
    return nullptr;
}

void Species::move_randomly(int world_width, int world_height, double speed, std::mt19937& rng) {
    if (!alive) {
        return;
    }
    std::uniform_real_distribution<> angle_dist(0, 2 * M_PI);
    double angle = angle_dist(rng);
    double dx = std::cos(angle) * speed;
    double dy = std::sin(angle) * speed;
    // 使用边界约束更新位置
    position.x = std::max(0.0, std::min((double)world_width, position.x + dx));
    position.y = std::max(0.0, std::min((double)world_height, position.y + dy));
}

void Species::age_one_step() {
    // 年龄增加一步
    age += 1;
    if (age >= max_age) die_from_old_age();
}

void Species::die(const std::string& reason) {
    // 带原因的死亡
    if (alive) {
        alive = false;
        death_reason = reason;
    }
}

void Species::die_from_old_age() { die("Old age"); }
void Species::die_from_starvation() { die("Starvation"); }
void Species::die_from_predation(const std::string& predator_name) { die("Predation by " + predator_name); }

std::optional<Position> Species::consume_pending_spawn_position() {
    if (!pending_spawn_position.has_value()) {
        return std::nullopt;
    }
    auto result = pending_spawn_position;
    pending_spawn_position.reset();
    return result;
}