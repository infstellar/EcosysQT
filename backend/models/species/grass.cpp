/*
物种数据模型 - Grass 类实现
定义生态系统中的草类，实现生产者逻辑
*/

#include "species.h"
#include "species_params.h"
#include "ecosystem.h"
#include "tracy/Tracy.hpp"
#include <random>
#include <algorithm>
#include <cmath>

// --- Grass ---
// 草类 - 生产者

Grass::Grass(Position pos, const GrassParams& params)
    : Producer(pos, params) {
    species_name = "grass";
}
double Grass::get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state) {
    // 计算根据本地竞争调整的增长率
    auto nearby_entities = ecosystem_state.get_nearby_species_broad(position, competition_radius);
    int nearby_grass_count = 0;
    for (const auto& entity : nearby_entities) {
        if (!entity || entity.get() == this || !entity->alive) {
            continue;
        }
        if (entity->species_name != "grass") {
            continue;
        }
        if (position.distance_to(entity->position) <= competition_radius) {
            nearby_grass_count++;
        }
    }

    // 计算竞争半径内的最大可能草量。假设每个草占据400.0的面积。
    double max_possible_grass = M_PI * (competition_radius * competition_radius) / 400.0;
    // 计算草的密度，即当前草数量与最大容量的比例，最大为1.0。
    double density = max_possible_grass > 0.0
        ? std::min(1.0, nearby_grass_count / max_possible_grass)
        : 0.0;
    // 根据密度计算竞争因子。密度越高，竞争越激烈，增长因子越低。
    double competition_factor = 1.0 - (std::pow(density, 0.3) * max_competition_effect);
    // 特殊情况：如果周围没有草，则加倍（可配置）生长速率以鼓励扩张。
    if (density <= 0.01) competition_factor = expansion_boost;
    // 将基础增长率乘以竞争因子，得到调整后的增长率。
    double adjusted_growth_rate = base_growth_rate * competition_factor;
    // 设置一个最低增长率，确保即使在激烈竞争下也能缓慢生长。
    double min_growth_rate = base_growth_rate * min_growth_factor;
    // 返回调整后的增长率，但不会低于设定的最低增长率。
    return std::max(min_growth_rate, adjusted_growth_rate);
}

void Grass::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    // 复用 Producer 的通用生长与繁殖逻辑（已按 tick 缩放）
    Producer::decide(ecosystem_state, rng);
}

void Grass::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    Species::apply(ecosystem_state);
    if (!alive) {
        pending_growth = 0.0;
        return;
    }

    energy = std::min(max_energy, energy + pending_growth);
    pending_growth = 0.0;
}

bool Grass::can_reproduce() const {
    // 兼容旧接口：判定是否满足基础条件。
    return Species::can_reproduce();
}

std::unique_ptr<Species> Grass::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}