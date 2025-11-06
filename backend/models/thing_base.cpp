/*
ThingBase 通用实现
从 Species 基类的通用逻辑复制并适配（无 Position/移动）
*/

#include "thing_base.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>

ThingBase::ThingBase(Position pos, double energy_, int max_age_, double reproduction_energy_cost_)
    : Species(pos, energy_, max_age_, reproduction_energy_cost_) {
    species_name = "ThingBase";
}

void ThingBase::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    Species::decide(ecosystem_state, rng);
}

void ThingBase::apply(const EcosystemState& ecosystem_state) {
    Species::apply(ecosystem_state);
}

bool ThingBase::can_reproduce() const {
    return Species::can_reproduce();
}

std::unique_ptr<Species> ThingBase::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}

void ThingBase::age_one_step() {
    Species::age_one_step();
}

void ThingBase::die(const std::string& reason) {
    Species::die(reason);
}

void ThingBase::die_from_old_age() { Species::die_from_old_age(); }
void ThingBase::die_from_starvation() { Species::die_from_starvation(); }
void ThingBase::die_from_predation(const std::string& predator_name) { Species::die_from_predation(predator_name); }