/*
物种数据模型 - Cow 类实现
定义生态系统中的牛类，实现初级消费者逻辑
*/

#include "species.h"
#include "species_params.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>

// --- Cow ---
// 牛类 - 初级消费者

Cow::Cow(Position pos, const CowParams& params)
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
             params.eating_range)
{
    species_name = "cow";
}

void Cow::update(const EcosystemState& ecosystem_state) {
    // 更新牛的状态
    Animal::update(ecosystem_state);
    if (!alive) return;
    intelligent_move(ecosystem_state);
    energy -= energy_consumption;
    
    // 使用通用查询接口寻找附近的草
    auto grass_in_range = ecosystem_state.get_species_in_range("grass", position, eating_range);
    
    // 吃草
    for (const auto& grass : grass_in_range) {
        if (grass->alive) {
            energy = std::min(max_energy, energy + grass->energy);
            grass->die_from_predation("Cow");
            break;
        }
    }
    
    if (energy <= 0) die_from_starvation();
}

void Cow::_eat_grass(const std::vector<Grass*>& grass_list) {
    // 吃草
    for (auto* grass : grass_list) {
        if (grass->alive && position.distance_to(grass->position) <= eating_range) {
            energy = std::min(max_energy, energy + grass->energy);
            grass->die_from_predation("Cow");
            break;
        }
    }
}

bool Cow::can_reproduce() const {
    // Check if can reproduce
    return Animal::can_reproduce();
}