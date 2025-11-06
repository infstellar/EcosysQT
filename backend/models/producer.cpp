/*
生产者（植物）基类实现
抽象生产者通用逻辑，供草/树/灌木等具体植物继承
*/

#include "producer.h"
#include "thing_base.h"
#include "species_params.h"
#include "ecosystem.h"
#include "tracy/Tracy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <vector>

// --- Producer ---

Producer::Producer(Position pos, const PlantParams& params)
    : ThingBase(pos, params.energy, params.max_age, params.reproduction_energy_cost),
      base_growth_rate(params.base_growth_rate),
      reproduction_chance(params.reproduction_chance),
      competition_radius(params.competition_radius),
      max_competition_effect(params.max_competition_effect),
      base_reproduction_cooldown(params.reproduction_cooldown),
      expansion_boost(params.expansion_boost),
      min_growth_factor(params.min_growth_factor),
      growth_time_scale_ms(params.growth_time_scale_ms) {
}

void Producer::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    ZoneScoped;
    ThingBase::decide(ecosystem_state, rng);
    if (!alive) return;
    // 单位制对齐：1秒=30 ticks；按推进的tick数量进行缩放，兼容不同帧率/速度
    const double dt_ticks = ecosystem_state.get_delta_ticks();
    static constexpr std::array<std::pair<int, int>, 4> kCardinalOffsets{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    }};
    static constexpr std::array<std::pair<int, int>, 4> kDiagonalOffsets{{
        {1, -1}, {1, 1}, {-1, 1}, {-1, -1}
    }};

    const bool use_diagonals = competition_radius >= 1.5;
    std::vector<std::pair<int, int>> neighbor_offsets;
    neighbor_offsets.reserve(use_diagonals ? 8 : 4);
    neighbor_offsets.insert(neighbor_offsets.end(), kCardinalOffsets.begin(), kCardinalOffsets.end());
    if (use_diagonals) {
        neighbor_offsets.insert(neighbor_offsets.end(), kDiagonalOffsets.begin(), kDiagonalOffsets.end());
    }

    int nearby_same_species = 0;
    for (const auto& [dx, dy] : neighbor_offsets) {
        const int nx = m_grid_x + dx;
        const int ny = m_grid_y + dy;
        if (!ecosystem_state.is_valid_grid_coord(nx, ny)) {
            continue;
        }
        const Tile& tile = ecosystem_state.get_tile(nx, ny);
        for (ThingBase* occupant : tile.things) {
            if (!occupant || !occupant->alive || occupant == this) {
                continue;
            }
            if (occupant->species_name == species_name) {
                ++nearby_same_species;
            }
        }
    }

    const double neighbor_slots = static_cast<double>(neighbor_offsets.size());
    double density = neighbor_slots > 0.0 ? std::min(1.0, nearby_same_species / neighbor_slots) : 0.0;
    double competition_factor = 1.0;
    if (density <= std::numeric_limits<double>::epsilon()) {
        competition_factor = expansion_boost;
    } else {
        competition_factor = 1.0 - (std::pow(density, 0.3) * max_competition_effect);
    }
    double adjusted_growth_rate = base_growth_rate * competition_factor;
    double min_growth_rate = base_growth_rate * min_growth_factor;
    pending_growth = std::max(min_growth_rate, adjusted_growth_rate) * dt_ticks;

    const bool ready_for_birth = alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
    if (ready_for_birth && !pending_spawn_position.has_value()) {
        std::uniform_real_distribution<> chance_dist(0.0, 1.0);
        if (chance_dist(rng) <= reproduction_chance) {
            std::vector<std::pair<int, int>> candidate_tiles;
            candidate_tiles.reserve(neighbor_offsets.size());
            for (const auto& [dx, dy] : neighbor_offsets) {
                const int nx = m_grid_x + dx;
                const int ny = m_grid_y + dy;
                if (!ecosystem_state.is_valid_grid_coord(nx, ny)) {
                    continue;
                }
                const Tile& tile = ecosystem_state.get_tile(nx, ny);
                if (tile.biome != BiomeType::LAND) {
                    continue;
                }
                const bool occupied = std::any_of(tile.things.begin(), tile.things.end(), [](ThingBase* existing) {
                    return existing && existing->alive;
                });
                if (!occupied) {
                    candidate_tiles.emplace_back(nx, ny);
                }
            }

            if (!candidate_tiles.empty()) {
                std::shuffle(candidate_tiles.begin(), candidate_tiles.end(), rng);
                const auto [spawn_x, spawn_y] = candidate_tiles.front();
                Position spawn_pos{static_cast<double>(spawn_x) + 0.5, static_cast<double>(spawn_y) + 0.5};
                pending_spawn_position = spawn_pos;
                energy -= reproduction_energy_cost;
                reproduction_cooldown = base_reproduction_cooldown;
                ecosystem_state.submit_interaction_request(AttemptToReproduceRequest{shared_from_this()});
            }
        }
    }
}

void Producer::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    ThingBase::apply(ecosystem_state);
    if (!alive) { pending_growth = 0.0; return; }
    energy = std::min(max_energy, energy + pending_growth);
    pending_growth = 0.0;
}

bool Producer::can_reproduce() const {
    return ThingBase::can_reproduce();
}

std::unique_ptr<Species> Producer::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}