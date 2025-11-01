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
             params.hunting_cooldown_duration,
             params.min_reproduction_age,
             params.reproduction_cooldown,
             params.eating_range) {
    species_name = "tiger";
}

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
    return Animal::can_reproduce();
}