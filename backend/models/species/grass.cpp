/*
物种数据模型 - Grass 类实现
定义生态系统中的草类，实现生产者逻辑
*/

#include "species.h"
#include "species_params.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <Eigen/Dense>

// --- Grass ---
// 草类 - 生产者

Grass::Grass(Position pos, const GrassParams& params)
    : Species(pos, params.energy, params.max_age, params.reproduction_energy_cost),
      base_growth_rate(params.base_growth_rate),
      reproduction_chance(params.reproduction_chance),
      competition_radius(params.competition_radius),
      max_competition_effect(params.max_competition_effect),
      base_reproduction_cooldown(params.reproduction_cooldown) {}

double Grass::calculate_nearby_grass_density_optimized(const EcosystemState& ecosystem_state) {
    // 使用Eigen矩阵计算附近区域的草密度
    const auto& ecosystem_data = ecosystem_state.get_ecosystem_state();
    const auto& positions = ecosystem_data.grass_positions_array;
    const auto& alive_grass_objects = ecosystem_data.alive_grass_objects;
    if (positions.rows() == 0) return 0.0;

    // 在alive_grass_objects中找到自己的索引以排除它
    int self_index = -1;
    for (size_t i = 0; i < alive_grass_objects.size(); ++i) {
        if (alive_grass_objects[i].get() == this) {
            self_index = static_cast<int>(i);
            break;
        }
    }

    // 从计算中排除自己
    Eigen::MatrixXd filtered;
    if (self_index >= 0 && positions.rows() > 1) {
        filtered = Eigen::MatrixXd(positions.rows() - 1, 2);
        int idx = 0;
        for (int i = 0; i < positions.rows(); ++i) {
            if (i != self_index) {
                filtered(idx, 0) = positions(i, 0);
                filtered(idx, 1) = positions(i, 1);
                ++idx;
            }
        }
    } else {
        filtered = positions;
    }
    if (filtered.rows() == 0) return 0.0;

    // 向量化距离计算
    Eigen::Vector2d self_pos(position.x, position.y);
    Eigen::VectorXd distances = (filtered.rowwise() - self_pos.transpose()).rowwise().norm();

    // 计算竞争半径内的草数量
    int nearby_grass_count = (distances.array() <= competition_radius).count();

    // 返回标准化密度 (0-1 比例)
    double max_possible_grass = M_PI * (competition_radius * competition_radius) / 400.0;
    double density = std::min(1.0, nearby_grass_count / max_possible_grass);
    return density;
}

double Grass::calculate_nearby_grass_density(const EcosystemState& ecosystem_state) {
    // 计算附近区域的草密度 (备用方法)
    const auto& ecosystem_data = ecosystem_state.get_ecosystem_state();
    if (ecosystem_data.grass_positions_array.rows() > 0)
        return calculate_nearby_grass_density_optimized(ecosystem_state);

    auto grass_it = ecosystem_data.species_lists.find("grass");
    if (grass_it == ecosystem_data.species_lists.end() || grass_it->second.empty()) return 0.0;
    
    const auto& grass_list = grass_it->second;
    std::vector<Position> alive_grass_positions;
    for (const auto& grass : grass_list) {
        if (grass.get() != this && grass->alive)
            alive_grass_positions.push_back(grass->position);
    }
    if (alive_grass_positions.empty()) return 0.0;

    int nearby_grass_count = 0;
    for (const auto& pos : alive_grass_positions) {
        double dist = position.distance_to(pos);
        if (dist <= competition_radius) nearby_grass_count++;
    }
    double max_possible_grass = M_PI * (competition_radius * competition_radius) / 100.0;
    double density = std::min(1.0, nearby_grass_count / max_possible_grass);
    return density;
}

double Grass::get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state) {
    // 计算根据本地竞争调整的增长率
    double density = calculate_nearby_grass_density(ecosystem_state);
    double competition_factor = 1.0 - (std::pow(density, 0.3) * max_competition_effect);
    if (density == 0) competition_factor = 2.0;
    double adjusted_growth_rate = base_growth_rate * competition_factor;
    double min_growth_rate = base_growth_rate * 0.01;
    return std::max(min_growth_rate, adjusted_growth_rate);
}

void Grass::update(const EcosystemState& ecosystem_state) {
    // 更新草的状态
    Species::update(ecosystem_state);
    if (!alive) return;
    double adjusted_growth_rate = get_competition_adjusted_growth_rate(ecosystem_state);
    energy = std::min(max_energy, energy + adjusted_growth_rate);
    if (age >= max_age) die();
}

bool Grass::can_reproduce() const {
    // Check if can reproduce
    return Species::can_reproduce() && (static_cast<double>(rand()) / RAND_MAX < reproduction_chance);
}

std::unique_ptr<Species> Grass::reproduce(const EcosystemState& ecosystem_state) {
    // 繁殖以创建新草
    Species::reproduce(ecosystem_state);
    if (!can_reproduce()) return nullptr;
    const auto& ecosystem_data = ecosystem_state.get_ecosystem_state();
    int world_width = ecosystem_data.world_width;
    int world_height = ecosystem_data.world_height;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist_x(-200, 200);
    std::uniform_real_distribution<> dist_y(-200, 200);

    double new_x = std::max(0.0, std::min((double)world_width, position.x + dist_x(gen)));
    double new_y = std::max(0.0, std::min((double)world_height, position.y + dist_y(gen)));
    if (new_x <= 0 || new_x >= world_width || new_y <= 0 || new_y >= world_height) return nullptr;

    energy -= reproduction_energy_cost;
    reproduction_cooldown = base_reproduction_cooldown;
    Position new_position{new_x, new_y};
    return g_species_factory.create("grass", new_position);
}