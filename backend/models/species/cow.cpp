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

void Cow::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    Animal::decide(ecosystem_state, rng);
    if (!alive) {
        return;
    }

    // 只在饥饿状态下尝试提交吃草请求，避免无谓竞争。
    if (hunger_state == HungerState::SATISFIED) {
        return;
    }

    auto nearby_entities = ecosystem_state.get_nearby_species_broad(position, eating_range);
    for (const auto& entity : nearby_entities) {
        if (!entity || !entity->alive) {
            continue;
        }
        if (entity->species_name != "grass") {
            continue;
        }
        if (position.distance_to(entity->position) > eating_range) {
            continue;
        }

        AttemptToEatRequest eat_request{shared_from_this(), entity};
        ecosystem_state.submit_interaction_request(std::move(eat_request));
        break;
    }
}

void Cow::apply(const EcosystemState& ecosystem_state) {
    Animal::apply(ecosystem_state);
}

bool Cow::can_reproduce() const {
    // Check if can reproduce
    return Animal::can_reproduce();
}