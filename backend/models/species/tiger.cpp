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
             params.wandering_duration,
             params.wander_radius,
             params.energy_efficiency) 
{
    species_name = "tiger";
}

void Tiger::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    Animal::decide(ecosystem_state, rng);
    if (!alive) {
        return;
    }

    const double desire = get_hunting_desire();
    if (desire <= 0.0) {
        return;
    }

    auto nearby_entities = ecosystem_state.get_nearby_species_broad(position, hunting_range);
    if (nearby_entities.empty()) {
        return;
    }

    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
    if (hunt_dist(rng) < hunting_success_rate * desire) {
        for (const auto& entity : nearby_entities) {
            if (!entity || !entity->alive) {
                continue;
            }
            if (entity->species_name != "cow") {
                continue;
            }
            if (position.distance_to(entity->position) > hunting_range) {
                continue;
            }

            AttemptToEatRequest hunt_request{shared_from_this(), entity};
            ecosystem_state.submit_interaction_request(std::move(hunt_request));
            start_hunting_cooldown();
            break;
        }
    }
}

void Tiger::apply(const EcosystemState& ecosystem_state) {
    Animal::apply(ecosystem_state);
}

bool Tiger::can_reproduce() const {
    // Check if can reproduce
    return Animal::can_reproduce();
}