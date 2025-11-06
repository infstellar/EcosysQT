/*
RaceBase 通用实现
从 Species 基类的通用逻辑复制并适配
*/

#include "race_base.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>

RaceBase::RaceBase(Position pos, double energy_, int max_age_, double reproduction_energy_cost_)
    : Species(pos, energy_, max_age_, reproduction_energy_cost_) {
    species_name = "RaceBase";
}

void RaceBase::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    // 保留与 Species 相同的通用生命周期逻辑
    Species::decide(ecosystem_state, rng);
}

void RaceBase::apply(const EcosystemState& ecosystem_state) {
    Species::apply(ecosystem_state);
}

bool RaceBase::can_reproduce() const {
    return Species::can_reproduce();
}

std::unique_ptr<Species> RaceBase::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}

void RaceBase::move_randomly(int world_width, int world_height, double speed, std::mt19937& rng) {
    if (!alive) return;
    std::uniform_real_distribution<> angle_dist(0, 2 * M_PI);
    double angle = angle_dist(rng);
    double dx = std::cos(angle) * speed;
    double dy = std::sin(angle) * speed;
    position.x = std::max(0.0, std::min((double)world_width, position.x + dx));
    position.y = std::max(0.0, std::min((double)world_height, position.y + dy));
}

void RaceBase::age_one_step() {
    Species::age_one_step();
}

void RaceBase::die(const std::string& reason) {
    Species::die(reason);
}

void RaceBase::die_from_old_age() { Species::die_from_old_age(); }
void RaceBase::die_from_starvation() { Species::die_from_starvation(); }
void RaceBase::die_from_predation(const std::string& predator_name) { Species::die_from_predation(predator_name); }