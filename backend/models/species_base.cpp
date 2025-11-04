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
    reproduction_energy_cost(reproduction_energy_cost) {}

void Species::update(const EcosystemState& ecosystem_state) {
    /*
    此函数只更新对象内部状态随时间变化，不涉及外部环境的交互。
     */
    if (!alive) return;
    // 减少繁殖冷却时间
    if (reproduction_cooldown > 0) reproduction_cooldown -= 1;
    age_one_step(); // 年龄增加一步
}

void Species::cross_species_update(const EcosystemState& ecosystem_state) {
    /*
    此函数更新对象内部状态随时间变化，涉及外部环境的交互。
     */

}

bool Species::can_reproduce() const {
    // 检查是否可以繁殖
    return alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
}

std::unique_ptr<Species> Species::reproduce(const EcosystemState& ecosystem_state) {
    // 基础物种不繁殖
    return nullptr;
}

void Species::move_randomly(int world_width, int world_height, double speed) {
    // 随机移动
    if (!alive) return;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> angle_dist(0, 2 * M_PI);
    double angle = angle_dist(gen);
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