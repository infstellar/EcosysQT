#include "bt_actions.h"
#include "animal.h"
#include "ecosystem.h"
#include "interaction_requests.h"
#include "thing_base.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>

namespace behavior::actions {

using namespace bt;

static inline double bb_get_double(Blackboard* bb, const std::string& key, double def_v = 0.0) {
    if (!bb) return def_v;
    auto it = bb->doubles.find(key);
    return it == bb->doubles.end() ? def_v : it->second;
}

static inline int bb_get_int(Blackboard* bb, const std::string& key, int def_v = 0) {
    if (!bb) return def_v;
    auto it = bb->ints.find(key);
    return it == bb->ints.end() ? def_v : it->second;
}

bt::Status FleeFromThreat(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;
    if (!ctx.blackboard) return Status::Failure;
    auto& bb = *ctx.blackboard;

    const std::string posx_key = params["pos_x_param"] ? params["pos_x_param"].as<std::string>() : std::string("threat_pos_x");
    const std::string posy_key = params["pos_y_param"] ? params["pos_y_param"].as<std::string>() : std::string("threat_pos_y");
    const std::string speed_key = params["speed_multiplier_param"] ? params["speed_multiplier_param"].as<std::string>() : std::string("flee_speed_multiplier");
    const std::string energy_key = params["energy_multiplier_param"] ? params["energy_multiplier_param"].as<std::string>() : std::string("flee_energy_multiplier");

    const double tx = bb_get_double(&bb, posx_key, self.position.x);
    const double ty = bb_get_double(&bb, posy_key, self.position.y);
    Position threat{tx, ty};
    Position dir{ self.position.x - threat.x, self.position.y - threat.y };
    const double inv_len = 1.0 / std::max(1e-9, std::sqrt(dir.x*dir.x + dir.y*dir.y));
    dir.x *= inv_len;
    dir.y *= inv_len;
    const double flee_step = std::max(self.get_step_distance_per_tick(), self.movement_speed);
    Position safe_spot{
        std::max(0.0, std::min(static_cast<double>(world->config.world_width), self.position.x + dir.x * flee_step)),
        std::max(0.0, std::min(static_cast<double>(world->config.world_height), self.position.y + dir.y * flee_step))
    };
    bb.doubles["target_pos_x"] = safe_spot.x;
    bb.doubles["target_pos_y"] = safe_spot.y;

    const double base_mul = bb_get_double(&bb, "current_speed_multiplier", 1.0);
    const double speed_mul = bb_get_double(&bb, speed_key, 1.5);
    const double energy_mul = bb_get_double(&bb, energy_key, 1.5);
    self.perform_step_move_to(safe_spot, world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul);

    self.clear_current_target();
    self.clear_path();
    self.clear_wander_target();
    self.clear_mating_target();
    bb.ints.erase("mate_target_id");
    bb.doubles.erase("mate_target_pos_x");
    bb.doubles.erase("mate_target_pos_y");
    bb.ints.erase("mating_timer_ticks");

    return Status::Running;
}

bt::Status WanderAnywhere(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    (void)params;
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    const int world_width = world->config.world_width;
    const int world_height = world->config.world_height;

    if (self.get_wander_target().has_value()) {
        const Position target = self.get_wander_target().value();
        const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
        const double speed_mul = bb_get_double(ctx.blackboard, "wander_speed_multiplier", 0.8);
        const double energy_mul = bb_get_double(ctx.blackboard, "wander_energy_multiplier", 0.6);
        self.perform_step_move_to(target, world_width, world_height, base_mul * speed_mul, energy_mul);
        const double arrival_threshold = std::max(0.2, self.get_current_step_distance() * 0.5);
        if (self.position.distance_to(target) <= arrival_threshold) {
            self.clear_wander_target();
        }
        return Status::Running;
    }

    auto& rng_local = world->get_thread_local_rng();
    std::uniform_real_distribution<> angle_dist(0.0, 2 * M_PI);
    std::uniform_real_distribution<> unit01(0.0, 1.0);
    for (int tries = 0; tries < 6 && !self.get_wander_target().has_value(); ++tries) {
        const double angle = angle_dist(rng_local);
        const double r = std::max(self.movement_speed, std::sqrt(unit01(rng_local)) * self.get_wander_radius());
        Position candidate{
            self.position.x + std::cos(angle) * r,
            self.position.y + std::sin(angle) * r
        };
        candidate.x = std::max(0.0, std::min(static_cast<double>(world_width), candidate.x));
        candidate.y = std::max(0.0, std::min(static_cast<double>(world_height), candidate.y));
        if (self.position.distance_to(candidate) < 1e-6) continue;
        self.set_wander_target(candidate);
    }
    if (!self.get_wander_target().has_value()) {
        std::uniform_real_distribution<> angle2(0.0, 2 * M_PI);
        const double a2 = angle2(rng_local);
        Position fallback{
            std::max(0.0, std::min(static_cast<double>(world_width), self.position.x + std::cos(a2) * self.movement_speed)),
            std::max(0.0, std::min(static_cast<double>(world_height), self.position.y + std::sin(a2) * self.movement_speed))
        };
        const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
        const double speed_mul = bb_get_double(ctx.blackboard, "wander_speed_multiplier", 0.8);
        const double energy_mul = bb_get_double(ctx.blackboard, "wander_energy_multiplier", 0.6);
        self.perform_step_move_to(fallback, world_width, world_height, base_mul * speed_mul, energy_mul);
        return Status::Running;
    }
    return Status::Running;
}

bt::Status ApproachOrMate(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;
    const std::string range_key = params["mating_range_param"] ? params["mating_range_param"].as<std::string>() : std::string("mating_range");

    auto nearest_mate_opt = self.find_available_mate(*world);
    if (!nearest_mate_opt.has_value()) return Status::Failure;
    auto mate = nearest_mate_opt.value();
    if (!mate || !mate->alive) return Status::Failure;

    if (ctx.blackboard) {
        ctx.blackboard->doubles["mate_target_pos_x"] = mate->position.x;
        ctx.blackboard->doubles["mate_target_pos_y"] = mate->position.y;
    }

    const double range = bb_get_double(ctx.blackboard, range_key, self.get_mating_range());
    const double dist = self.position.distance_to(mate->position);
    if (dist <= range) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "Submitting mate request: male pos=({:.1f},{:.1f}), female pos=({:.1f},{:.1f}), dist={:.2f}, range={:.2f}",
            self.position.x, self.position.y, mate->position.x, mate->position.y, dist, range);
        world->submit_interaction_request(AttemptToMateRequest{mate, std::dynamic_pointer_cast<Animal>(self.shared_from_this())});
        self.set_skip_movement(true);
        return Status::Running;
    }

    self.set_mating_target(mate->position);
    self.set_mating_intent_lock_ticks(self.get_mating_intent_lock_duration());
    self.set_current_target(self.get_mating_target());
    self.plan_path_to_target(*world, self.get_current_target());
    if (ctx.blackboard && self.get_current_target().has_value()) {
        ctx.blackboard->doubles["target_pos_x"] = self.get_current_target()->x;
        ctx.blackboard->doubles["target_pos_y"] = self.get_current_target()->y;
    }
    const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
    const double speed_mul = bb_get_double(ctx.blackboard, "mate_speed_multiplier", 1.0);
    const double energy_mul = bb_get_double(ctx.blackboard, "mate_energy_multiplier", 1.0);
    self.perform_step_move_path(world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul);
    return Status::Running;
}

bt::Status EatNearbyThing(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    const std::string thing = params["thing"] ? params["thing"].as<std::string>() : (params["kind"] ? params["kind"].as<std::string>() : std::string("grass"));
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("eating_range");
    const double eat_range = bb_get_double(ctx.blackboard, range_key, self.eating_range);
    if (eat_range <= 0.0) return Status::Failure;

    auto nearby_things = world->get_things_in_range(thing, self.position, eat_range);
    for (const auto& t : nearby_things) {
        if (!t || !t->alive) continue;
        world->submit_interaction_request(AttemptToEatThingRequest{self.shared_from_this(), t});
        self.set_skip_movement(true);
        return Status::Success;
    }
    return Status::Failure;
}

bt::Status HuntNearbyRace(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;
    const std::string race = params["race"] ? params["race"].as<std::string>() : (params["kind"] ? params["kind"].as<std::string>() : std::string("cow"));
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("hunting_range");
    const std::string rate_key = params["success_rate_param"] ? params["success_rate_param"].as<std::string>() : std::string("hunting_success_rate");
    const double range = bb_get_double(ctx.blackboard, range_key, self.hunting_range);
    const double desire = self.get_hunting_desire();
    auto& rng_local = world->get_thread_local_rng();
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
    const double rate = bb_get_double(ctx.blackboard, rate_key, self.hunting_success_rate);
    if (range > 0.0 && self.hunting_cooldown <= 0 && desire > 0.0) {
        if (hunt_dist(rng_local) < rate * desire) {
            for (auto& wptr : self.get_cached_food_races_snapshot()) {
                auto r = wptr.lock();
                if (!r || !r->alive) continue;
                if (r.get() == &self) continue;
                if (r->species_name != race) continue;
                if (self.position.distance_to(r->position) > range) continue;
                world->submit_interaction_request(AttemptToEatRaceRequest{self.shared_from_this(), r});
                self.start_hunting_cooldown();
                self.set_forage_intent_lock_ticks(self.get_forage_intent_lock_duration());
                self.set_skip_movement(true);
                return Status::Running;
            }
        }
    }
    return Status::Failure;
}

bt::Status SelectTargetPoint(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    (void)params;
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();
    const double detect_range = self.get_detection_range();
    for (auto& wptr : self.get_cached_food_races_snapshot()) {
        auto race = wptr.lock();
        if (!race || !race->alive) continue;
        if (std::find(self.food_types.begin(), self.food_types.end(), race->species_name) == self.food_types.end()) continue;
        double distance = self.position.distance_to(race->position);
        if (distance <= detect_range && distance < min_distance) {
            min_distance = distance;
            nearest_food = race->position;
        }
    }
    for (auto& wptr : self.get_cached_food_things_snapshot()) {
        auto thing = wptr.lock();
        if (!thing || !thing->alive) continue;
        if (std::find(self.food_types.begin(), self.food_types.end(), thing->species_name) == self.food_types.end()) continue;
        double distance = self.position.distance_to(thing->position);
        if (distance <= detect_range && distance < min_distance) {
            min_distance = distance;
            nearest_food = thing->position;
        }
    }

    if (nearest_food.has_value()) {
        self.set_current_target(nearest_food.value());
        if (ctx.blackboard) {
            ctx.blackboard->doubles["target_pos_x"] = nearest_food->x;
            ctx.blackboard->doubles["target_pos_y"] = nearest_food->y;
        }
        return Status::Success;
    }
    self.clear_current_target();
    self.clear_path();
    return Status::Failure;
}

bt::Status PlanPathToTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    if (!self.get_current_target().has_value()) return Status::Failure;
    self.plan_path_to_target(*world, self.get_current_target());

    const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
    const std::string speed_key = params["speed_multiplier_key"] ? params["speed_multiplier_key"].as<std::string>() : std::string("chase_speed_multiplier");
    const std::string energy_key = params["energy_multiplier_key"] ? params["energy_multiplier_key"].as<std::string>() : std::string("chase_energy_multiplier");
    const double speed_mul = bb_get_double(ctx.blackboard, speed_key, 1.0);
    const double energy_mul = bb_get_double(ctx.blackboard, energy_key, 1.0);
    self.perform_step_move_path(world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul);
    self.set_forage_intent_lock_ticks(self.get_forage_intent_lock_duration());
    return Status::Running;
}

} // namespace behavior::actions