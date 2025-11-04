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
             params.eating_range,
             params.energy, // Use initial energy as max_energy
             params.satisfied_threshold_ratio,
             params.starving_threshold_ratio,
             params.wandering_duration, 
             params.energy_efficiency)
{
    species_name = "cow";
}

void Cow::update(const EcosystemState& ecosystem_state) {
    Animal::update(ecosystem_state);
    if (!alive) return;

    if (hunger_state != HungerState::SATISFIED) {
        auto grass_in_range = ecosystem_state.get_species_in_range("grass", position, eating_range);
        for (const auto& grass : grass_in_range) {
            if (grass->alive) {
                energy = std::min(max_energy, energy + (grass->energy * this->energy_efficiency));
                grass->die_from_predation("Cow");
                break;
            }
        }
    }
}

bool Cow::can_reproduce() const {
    // Check if can reproduce
    return Animal::can_reproduce();
}