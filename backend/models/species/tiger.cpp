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
             params.eating_range,
             params.energy, // Use initial energy as max_energy
             params.satisfied_threshold_ratio,
             params.starving_threshold_ratio,
             params.wandering_duration) {
    species_name = "tiger";
}

void Tiger::update(const EcosystemState& ecosystem_state) {
    Animal::update(ecosystem_state);
    if (!alive) return;

    double desire = get_hunting_desire();
    if (desire <= 0) return;

    auto cows_in_range = ecosystem_state.get_species_in_range("cow", position, hunting_range);
    if (cows_in_range.empty()) return;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);

    if (hunt_dist(gen) < hunting_success_rate * desire) {
        for (const auto& cow : cows_in_range) {
            if (cow->alive) {
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