#include "population_manager.h"

#include "ecosystem.h"
#include "race_factory.h"
#include "species_config_provider.h"
#include "thing_factory.h"
#include "thing_base.h"
#include "race_base.h"
#include "tile.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "tracy/Tracy.hpp"

void PopulationManager::apply_changes(EcosystemState& state) {
    ZoneScoped;
    auto& resolution = state.m_resolution_state;
    auto& reproduction_parents = resolution.reproduction_parents;
    auto& thing_reproduction_parents = resolution.thing_reproduction_parents;
    auto& race_marked_for_death = resolution.race_marked_for_death;
    auto& race_energy_changes = resolution.race_energy_changes;

    std::unordered_map<std::string, std::vector<std::shared_ptr<RaceBase>>> newborns_by_species;
    newborns_by_species.reserve(reproduction_parents.size());
    std::unordered_map<std::string, int> thing_birth_counts;
    std::unordered_map<std::string, int> thing_death_counts;
    auto logger = spdlog::get("ecosim");
    {
        ZoneScopedN("Apply Race Reproduction");
        for (auto& parent : reproduction_parents) {
            if (!parent || !parent->alive) {
                continue;
            }

            const auto spawn_position = parent->consume_pending_spawn_position();
            if (!spawn_position.has_value()) {
                continue;
            }

            auto offspring_unique = g_race_factory.create(parent->species_name, spawn_position.value(), state.get_thread_local_rng());
            if (!offspring_unique) {
                continue;
            }

            std::shared_ptr<RaceBase> offspring = std::move(offspring_unique);
            // 依据物种配置设置新生个体初始能量比例（仅针对动物）
            if (auto provider = g_race_factory.get_config_provider()) {
                try {
                    AnimalParams p = provider->get_animal_params(parent->species_name);
                    const double ratio = std::clamp(p.newborn_energy_ratio, 0.0, 1.0);
                    offspring->energy = std::min(offspring->max_energy, offspring->max_energy * ratio);
                } catch (...) {
                    // 对于非动物或配置缺失，保持默认能量
                }
            }
            offspring->position = spawn_position.value();
            newborns_by_species[parent->species_name].push_back(std::move(offspring));
        }
    }
    
    {
        ZoneScopedN("Apply Race Energy Changes");
        for (const auto& name : state.races_registry.get_all_species_names()) {
            auto& list = state.races_registry.get_species_list(name);

            int dead_count = 0;
            for (auto& individual : list) {
                if (!individual) {
                    continue;
                }

                if (race_marked_for_death.find(individual.get()) != race_marked_for_death.end()) {
                    if (individual->alive) {
                        individual->alive = false;
                        ++dead_count;
                    }
                    continue;
                }

                if (auto energy_it = race_energy_changes.find(individual.get());
                    energy_it != race_energy_changes.end()) {
                    const double delta = energy_it->second;
                    const double prev = individual->energy;
                    individual->energy = std::min(individual->max_energy, prev + delta);
                    if (logger) {
                        logger->info("[Finalize Energy] '{}' id={} +{:.1f} -> {:.1f} (max: {:.1f})",
                                    individual->species_name,
                                    reinterpret_cast<std::uintptr_t>(individual.get()),
                                    delta,
                                    individual->energy,
                                    individual->max_energy);
                    }
                }
            }

            if (dead_count > 0) {
                state.deaths.increment(name, dead_count);
                if (logger) {
                    logger->info("\xF0\x9F\x92\x80 {} {} individuals died", dead_count, name);
                }
            }

            state.races_registry.filter_alive(name);

            auto newborn_it = newborns_by_species.find(name);
            if (newborn_it != newborns_by_species.end() && !newborn_it->second.empty()) {
                state.races_registry.extend_individuals(name, newborn_it->second);
                state.births.increment(name, static_cast<int>(newborn_it->second.size()));
                if (logger) {
                    logger->info("{} {} new {} individuals born",
                        (name == "grass" ? "\xF0\x9F\x8C\xB1" : name == "cow" ? "\xF0\x9F\x90\x84" : "\xF0\x9F\x90\x85"),
                        newborn_it->second.size(), name);
                }
            }
        }
    }

    {
        ZoneScopedN("Things Reproduction");
        auto remove_it = std::remove_if(state.m_all_things.begin(), state.m_all_things.end(),
        [&state, &thing_death_counts, logger](const std::shared_ptr<ThingBase>& thing) {
            if (!thing) {
                return true;
            }
            if (thing->alive) {
                return false;
            }
            auto it = state.m_thing_counts.find(thing->species_name);
            if (it != state.m_thing_counts.end() && it->second > 0) {
                --(it->second);
            }
            ++thing_death_counts[thing->species_name];
            if (logger) {
                logger->info("[Finalize] Removing '{}' at ({:.1f},{:.1f})",
                             thing->species_name, thing->position.x, thing->position.y);
            }
            state.detach_thing_from_tile(*thing);
            return true;
        });
        state.m_all_things.erase(remove_it, state.m_all_things.end());

        if (state.config.world_width > 0 && state.config.world_height > 0) {
            const int max_x = state.config.world_width - 1;
            const int max_y = state.config.world_height - 1;
            for (auto& parent : thing_reproduction_parents) {
                if (!parent || !parent->alive) {
                    continue;
                }
                const auto spawn_position = parent->consume_pending_spawn_position();
                if (!spawn_position.has_value()) {
                    continue;
                }

                int tile_x = static_cast<int>(std::floor(spawn_position->x));
                int tile_y = static_cast<int>(std::floor(spawn_position->y));
                tile_x = std::clamp(tile_x, 0, max_x);
                tile_y = std::clamp(tile_y, 0, max_y);

                if (!state.world_grid().is_valid_coord(tile_x, tile_y)) {
                    continue;
                }

                Tile& tile = state.world_grid().get_tile(tile_x, tile_y);
                const bool tile_available = std::none_of(tile.things.begin(), tile.things.end(),
                    [](ThingBase* existing) {
                        return existing != nullptr && existing->alive;
                    });
                if (!tile_available) {
                    continue;
                }

                Position world_pos{static_cast<double>(tile_x) + 0.5, static_cast<double>(tile_y) + 0.5};
                auto offspring_unique = g_thing_factory.create(parent->species_name, world_pos, state.get_thread_local_rng());
                if (!offspring_unique) {
                    continue;
                }

                std::shared_ptr<ThingBase> offspring(std::move(offspring_unique));
                // 依据物种配置设置新生植物的初始能量比例
                if (auto provider = g_thing_factory.get_config_provider()) {
                    try {
                        PlantParams p = provider->get_plant_params(parent->species_name);
                        const double ratio = std::clamp(p.newborn_energy_ratio, 0.0, 1.0);
                        offspring->energy = std::min(offspring->max_energy, offspring->max_energy * ratio);
                    } catch (...) {
                        // 配置缺失或非植物：保持默认能量
                    }
                }
                offspring->position = world_pos;
                offspring->m_grid_x = tile_x;
                offspring->m_grid_y = tile_y;
                state.attach_thing_to_world(offspring);
                ++thing_birth_counts[parent->species_name];
            }
        }
    }

    {
        ZoneScopedN("Apply Things Birth and Death Counts");
        for (const auto& [species, count] : thing_birth_counts) {
            if (count <= 0) {
                continue;
            }
            state.births.increment(species, count);
            if (logger) {
                logger->info("\xF0\x9F\x8C\xB1 {} new {} things born", count, species);
            }
        }

        for (const auto& [species, count] : thing_death_counts) {
            if (count <= 0) {
                continue;
            }
            state.deaths.increment(species, count);
            if (logger) {
                logger->info("\xF0\x9F\xA5\x80 {} {} things removed", count, species);
            }
        }
    }

    

    

}
