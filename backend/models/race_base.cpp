/*
RaceBase 通用实现
从 Species 基类的通用逻辑复制并适配
*/

#include "race_base.h"
#include "ecosystem.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

RaceBase::RaceBase(Position pos, double energy_, int max_age_, double reproduction_energy_cost_)
    : position(pos),
      energy(energy_),
      max_energy(energy_ * 4),
      age(0),
      max_age(max_age_),
      alive(true),
      reproduction_cooldown(0),
      death_reason(""),
      species_name("RaceBase"),
      reproduction_energy_cost(reproduction_energy_cost_),
      pending_spawn_position(std::nullopt) {}

void RaceBase::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    (void)ecosystem_state;
    (void)rng;
    if (!alive) {
        return;
    }
    if (reproduction_cooldown > 0) {
        reproduction_cooldown -= 1;
    }
    age += 1;
    if (age >= max_age) {
        die("Old age");
    }
}

void RaceBase::apply(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
}

bool RaceBase::can_reproduce() const {
    return alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
}

std::unique_ptr<RaceBase> RaceBase::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}

void RaceBase::move_randomly(int world_width, int world_height, double speed, std::mt19937& rng) {
    if (!alive) return;
    std::uniform_real_distribution<> angle_dist(0, 2 * M_PI);
    double angle = angle_dist(rng);
    double dx = std::cos(angle) * speed;
    double dy = std::sin(angle) * speed;
    position.x = std::max(0.0, std::min(static_cast<double>(world_width), position.x + dx));
    position.y = std::max(0.0, std::min(static_cast<double>(world_height), position.y + dy));
}

void RaceBase::age_one_step() {
    age += 1;
    if (age >= max_age) {
        die_from_old_age();
    }
}

void RaceBase::die(const std::string& reason) {
    if (alive) {
        alive = false;
        death_reason = reason;
    }
}

void RaceBase::die_from_old_age() { die("Old age"); }
void RaceBase::die_from_starvation() { die("Starvation"); }
void RaceBase::die_from_predation(const std::string& predator_name) { die("Predation by " + predator_name); }

std::optional<Position> RaceBase::consume_pending_spawn_position() {
    if (!pending_spawn_position.has_value()) {
        return std::nullopt;
    }
    auto result = pending_spawn_position;
    pending_spawn_position.reset();
    return result;
}