/*
ThingBase 通用实现
从 Species 基类的通用逻辑复制并适配（无 Position/移动）
*/

#include "thing_base.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>

ThingBase::ThingBase(double energy_, int max_age_, double reproduction_energy_cost_)
    : energy(energy_),
      max_energy(energy_ * 4),
      age(0),
      max_age(max_age_),
      alive(true),
      reproduction_cooldown(0),
      death_reason(""),
      species_name("ThingBase"),
      reproduction_energy_cost(reproduction_energy_cost_),
      pending_spawn_position(std::nullopt) {}

void ThingBase::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    (void)ecosystem_state;
    (void)rng;
    if (!alive) return;
    if (reproduction_cooldown > 0) {
        reproduction_cooldown -= 1;
    }
    age += 1;
    if (age >= max_age) {
        die("Old age");
    }
}

void ThingBase::apply(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
}

bool ThingBase::can_reproduce() const {
    return alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
}

std::unique_ptr<ThingBase> ThingBase::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}

void ThingBase::age_one_step() {
    age += 1;
    if (age >= max_age) die_from_old_age();
}

void ThingBase::die(const std::string& reason) {
    if (alive) {
        alive = false;
        death_reason = reason;
    }
}

void ThingBase::die_from_old_age() { die("Old age"); }
void ThingBase::die_from_starvation() { die("Starvation"); }
void ThingBase::die_from_predation(const std::string& predator_name) { die("Predation by " + predator_name); }

std::optional<Position> ThingBase::consume_pending_spawn_position() {
    if (!pending_spawn_position.has_value()) {
        return std::nullopt;
    }
    auto result = pending_spawn_position;
    pending_spawn_position.reset();
    return result;
}