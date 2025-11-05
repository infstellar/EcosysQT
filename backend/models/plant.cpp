/*
植物基类实现
抽象生产者通用逻辑，供草/树/灌木等具体植物继承
*/

#include "species.h"
#include "species_params.h"
#include "ecosystem.h"
#include "tracy/Tracy.hpp"
#include <algorithm>
#include <cmath>
#include <random>

// --- Plant ---

Plant::Plant(Position pos, const PlantParams& params)
    : Species(pos, params.energy, params.max_age, params.reproduction_energy_cost),
      base_growth_rate(params.base_growth_rate),
      reproduction_chance(params.reproduction_chance),
      competition_radius(params.competition_radius),
      max_competition_effect(params.max_competition_effect),
      base_reproduction_cooldown(params.reproduction_cooldown),
      expansion_boost(params.expansion_boost),
      min_growth_factor(params.min_growth_factor),
      growth_time_scale_ms(params.growth_time_scale_ms) {
}

double Plant::get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state) {
    // 计算根据本地竞争调整的增长率（同类植物间竞争）
    auto nearby_entities = ecosystem_state.get_nearby_species_broad(position, competition_radius);
    int nearby_same_plant_count = 0;
    for (const auto& entity : nearby_entities) {
        if (!entity || entity.get() == this || !entity->alive) continue;
        if (entity->species_name != species_name) continue;
        if (position.distance_to(entity->position) <= competition_radius) {
            nearby_same_plant_count++;
        }
    }

    // 计算竞争半径内的最大可能植物数量。假设单位占地面积与草一致。
    double max_possible = M_PI * (competition_radius * competition_radius) / 400.0;
    double density = max_possible > 0.0 ? std::min(1.0, nearby_same_plant_count / max_possible) : 0.0;
    double competition_factor = 1.0 - (std::pow(density, 0.3) * max_competition_effect);
    if (density <= 0.01) competition_factor = expansion_boost;
    double adjusted_growth_rate = base_growth_rate * competition_factor;
    double min_growth_rate = base_growth_rate * min_growth_factor;
    return std::max(min_growth_rate, adjusted_growth_rate);
}

void Plant::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    ZoneScoped;
    Species::decide(ecosystem_state, rng);
    if (!alive) return;

    const double dt_scale = ecosystem_state.get_delta_time_ms() / std::max(1e-9, growth_time_scale_ms);
    pending_growth = get_competition_adjusted_growth_rate(ecosystem_state) * dt_scale;

    const bool ready_for_birth = alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
    if (ready_for_birth && !pending_spawn_position.has_value()) {
        std::uniform_real_distribution<> chance_dist(0.0, 1.0);
        if (chance_dist(rng) <= reproduction_chance) {
            std::uniform_real_distribution<> dist_x(-200.0, 200.0);
            std::uniform_real_distribution<> dist_y(-200.0, 200.0);
            const double new_x = std::max(0.0, std::min(static_cast<double>(ecosystem_state.config.world_width), position.x + dist_x(rng)));
            const double new_y = std::max(0.0, std::min(static_cast<double>(ecosystem_state.config.world_height), position.y + dist_y(rng)));
            if (new_x > 0.0 && new_x < ecosystem_state.config.world_width &&
                new_y > 0.0 && new_y < ecosystem_state.config.world_height) {
                pending_spawn_position = Position{new_x, new_y};
                energy -= reproduction_energy_cost;
                reproduction_cooldown = base_reproduction_cooldown;
                ecosystem_state.submit_interaction_request(AttemptToReproduceRequest{shared_from_this()});
            }
        }
    }
}

void Plant::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    Species::apply(ecosystem_state);
    if (!alive) { pending_growth = 0.0; return; }
    energy = std::min(max_energy, energy + pending_growth);
    pending_growth = 0.0;
}

bool Plant::can_reproduce() const {
    return Species::can_reproduce();
}

std::unique_ptr<Species> Plant::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}