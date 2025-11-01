/*
物种数据模型 - Tiger 类实现
定义生态系统中的老虎类，实现次级消费者逻辑
*/

#include "species.h"
#include "species_params.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>

// --- Tiger ---
// 老虎类 - 次级消费者

Tiger::Tiger(Position pos, const TigerParams& params)
    : Animal(pos,
             params.energy,
             params.max_age,
             params.reproduction_energy_cost,
             params.movement_speed,
             params.energy_consumption,
             params.hunting_range,
             params.hunting_success_rate,
             params.detection_range,
             params.food_types,
             params.hunting_cooldown_duration),
      min_reproduction_age(params.min_reproduction_age),
      base_reproduction_cooldown(params.reproduction_cooldown) {}

void Tiger::update(const EcosystemState& ecosystem_state) {
    // 更新老虎状态
    Animal::update(ecosystem_state);
    if (energy <= reproduction_energy_cost / 3) {
        hunting_success_rate = 0.2 + 0.6 * (1.0 - age / (double)max_age);
    } else {
        hunting_success_rate = 0.2;
    }
    if (!alive) return;
    intelligent_move(ecosystem_state);
    energy -= energy_consumption;
    
    // 使用通用查询接口寻找附近的牛
    auto cows_in_range = ecosystem_state.get_species_in_range("cow", position, hunting_range);
    
    // 狩猎牛
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);

    for (const auto& cow : cows_in_range) {
        if (cow->alive) {
            if (hunt_dist(gen) < hunting_success_rate) {
                energy = std::min(max_energy, energy + cow->energy);
                cow->die_from_predation("Tiger");
                start_hunting_cooldown();
                break;
            }
        }
    }
    
    if (energy <= 0) die_from_starvation();
}

void Tiger::_hunt_cows(const std::vector<Cow*>& cow_list) {
    // 狩猎牛
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);

    for (auto* cow : cow_list) {
        if (cow->alive && position.distance_to(cow->position) <= hunting_range) {
            if (hunt_dist(gen) < hunting_success_rate) {
                energy = std::min(max_energy, energy + cow->energy);
                cow->die_from_predation("Tiger");
                start_hunting_cooldown();
                break;
            }
        }
    }
}

bool Tiger::can_reproduce() const {
    // Check if can reproduce
    return Animal::can_reproduce() && age > min_reproduction_age;
}

std::unique_ptr<Species> Tiger::reproduce(const EcosystemState& ecosystem_state) {
    // 繁殖以创建新老虎
    Animal::reproduce(ecosystem_state);
    if (!can_reproduce()) return nullptr;
    energy -= reproduction_energy_cost;
    reproduction_cooldown = base_reproduction_cooldown;
    const auto& ecosystem_data = ecosystem_state.get_ecosystem_state();
    int world_width = ecosystem_data.world_width;
    int world_height = ecosystem_data.world_height;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist_x(-40, 40);
    std::uniform_real_distribution<> dist_y(-40, 40);

    double new_x = std::max(0.0, std::min((double)world_width, position.x + dist_x(gen)));
    double new_y = std::max(0.0, std::min((double)world_height, position.y + dist_y(gen)));
    Position new_position{new_x, new_y};
    return g_species_factory.create("tiger", new_position);
}