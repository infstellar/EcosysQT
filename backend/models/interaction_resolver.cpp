#include "interaction_resolver.h"

#include "animal.h"
#include "ecosystem.h"
#include "race_base.h"
#include "thing_base.h"

#include <algorithm>
#include <limits>
#include <string>

#include <spdlog/spdlog.h>

void InteractionResolutionState::clear() {
    race_energy_changes.clear();
    race_marked_for_death.clear();
    thing_energy_changes.clear();
    thing_marked_for_death.clear();
    reproduction_parents.clear();
    thing_reproduction_parents.clear();
}

void InteractionResolver::dispatch_request(const InteractionRequest& request,
                                           EcosystemState& state,
                                           InteractionResolutionState& results) {
    std::visit([this, &state, &results](auto&& req) {
        handle_request(req, state, results);
    }, request);
}

void InteractionResolver::handle_request(const AttemptToEatThingRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    auto& initiator = req.initiator;
    auto& target = req.target;
    if (!initiator || !target) return;
    if (!initiator->alive || !target->alive) return;

    auto logger = spdlog::get("ecosim");
    if (results.thing_marked_for_death.find(target.get()) != results.thing_marked_for_death.end()) {
        if (logger) {
            logger->info("[Resolve EatThing] Duplicate request ignored: initiator='{}' target='{}' pos=({:.1f},{:.1f})",
                         initiator->species_name, target->species_name,
                         target->position.x, target->position.y);
        }
        return;
    }

    results.thing_marked_for_death.insert(target.get());

    double efficiency = 1.0;
    if (auto* animal = dynamic_cast<Animal*>(initiator.get())) {
        efficiency = std::max(0.0, animal->energy_efficiency);
    }
    results.race_energy_changes[initiator.get()] += (target->energy * efficiency);

    if (logger) {
        logger->info("[Resolve EatThing] Accepted: '{}' eats '{}' at ({:.1f},{:.1f}); energy +{:.1f}",
                     initiator->species_name, target->species_name,
                     target->position.x, target->position.y,
                     target->energy);
    }

    target->die_from_predation(initiator->species_name);
}

void InteractionResolver::handle_request(const DamageRaceRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    auto& attacker = req.attacker;
    auto& target = req.target;
    const double damage = std::max(0.0, req.damage);
    if (!attacker || !target) return;
    if (!attacker->alive || !target->alive) return;

    if (results.race_marked_for_death.find(target.get()) != results.race_marked_for_death.end()) return;

    const std::string source = attacker ? attacker->species_name : std::string("Unknown");
    const double pre_death_energy = target->energy;

    double efficiency = 1.0;
    if (auto* animal = dynamic_cast<Animal*>(attacker.get())) {
        efficiency = std::max(0.0, animal->energy_efficiency);
    }

    target->take_damage(damage, source);

    auto logger = spdlog::get("ecosim");
    if (!target->alive) {
        results.race_marked_for_death.insert(target.get());
        results.race_energy_changes[attacker.get()] += (pre_death_energy * efficiency);
        if (logger) {
            logger->info("[Resolve DamageRace] '{}' dealt {:.1f} to '{}' -> KILLED. Energy gained: {:.1f}",
                         source, damage, target->species_name, (pre_death_energy * efficiency));
        }
    } else {
        if (logger) {
            logger->info("[Resolve DamageRace] '{}' dealt {:.1f} to '{}' (hp={:.1f}/{:.1f})",
                         source, damage, target->species_name, target->hp_current, target->hp_max);
        }
    }
}

void InteractionResolver::handle_request(const DamageThingRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    auto& attacker = req.attacker;
    auto& target = req.target;
    if (!attacker || !target) return;
    if (!attacker->alive || !target->alive) return;

    if (results.thing_marked_for_death.find(target.get()) != results.thing_marked_for_death.end()) return;

    const std::string source = attacker ? attacker->species_name : std::string("Unknown");
    results.thing_marked_for_death.insert(target.get());
    target->die("Destroyed by " + source);

    if (auto logger = spdlog::get("ecosim")) {
        logger->info("[Resolve DamageThing] '{}' destroyed '{}' at ({:.1f},{:.1f})",
                     source, target->species_name, target->position.x, target->position.y);
    }
}

void InteractionResolver::handle_request(const AttemptToReproduceRaceRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    if (req.parent && req.parent->alive) {
        results.reproduction_parents.push_back(req.parent);
    }
}

void InteractionResolver::handle_request(const AttemptToReproduceThingRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    if (req.parent && req.parent->alive) {
        results.thing_reproduction_parents.push_back(req.parent);
    }
}

void InteractionResolver::handle_request(const AttemptToMateRequest& req,
                                         EcosystemState& state,
                                         InteractionResolutionState& results) {
    (void)state;
    (void)results;
    auto& female = req.female;
    auto& male = req.male;

    const bool female_alive = (female && female->alive);
    const bool male_alive = (male && male->alive);
    const bool female_can = (female && female->can_reproduce());
    const bool male_can = (male && male->can_reproduce());
    const double dist = (female && male)
        ? female->position.distance_to(male->position)
        : std::numeric_limits<double>::quiet_NaN();

    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
        "AttemptToMateRequest: female_alive={}, male_alive={}, female_can={}, male_can={}, dist={:.2f}",
        female_alive, male_alive, female_can, male_can, dist);

    if (female_alive && male_alive && female_can && male_can) {
        female->begin_mating_with(male);
        male->begin_mating_with(female);
        female->become_pregnant();
        male->start_reproduction_cooldown();
        female->energy -= female->reproduction_energy_cost;
        male->energy -= male->reproduction_energy_cost;

        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "Mating accepted: male(age={},energy={:.1f}) female(age={},energy={:.1f}) dist={:.2f}",
            male ? male->age : -1, male ? male->energy : 0.0,
            female ? female->age : -1, female ? female->energy : 0.0,
            dist);
    } else {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "Mating rejected: conditions not met (female_alive={}, male_alive={}, female_can={}, male_can={})",
            female_alive, male_alive, female_can, male_can);
    }
}
