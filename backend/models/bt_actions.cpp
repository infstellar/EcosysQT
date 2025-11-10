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
    const double base_energy_mul = bb_get_double(&bb, "current_energy_multiplier", 1.0);
    if (self.is_pregnant) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "[Move Flee] Pregnant speed: base_mul={:.2f} speed_mul={:.2f} final_mul={:.2f}",
            base_mul, speed_mul, base_mul * speed_mul);
    }
    self.perform_step_move_to(safe_spot, world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul * base_energy_mul);

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
    // 进入游荡的状态日志（降级为 Debug，避免刷屏）
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
        "[WANDER] '{}' entered Wander. IsPregnant={} skip_movement={} pos=({:.1f},{:.1f}) has_target={}",
        self.species_name, self.is_pregnant, self.get_skip_movement(), self.position.x, self.position.y, self.get_wander_target().has_value());
    if (self.get_skip_movement()) {
        return Status::Failure;
    }

    const int world_width = world->config.world_width;
    const int world_height = world->config.world_height;

    if (self.get_wander_target().has_value()) {
        const Position target = self.get_wander_target().value();
        const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
        const double speed_mul = bb_get_double(ctx.blackboard, "wander_speed_multiplier", 0.8);
        const double energy_mul = bb_get_double(ctx.blackboard, "wander_energy_multiplier", 0.6);
        if (self.is_pregnant) {
            // 保留孕期速度计算，但不输出 info 日志
        }
        const Position prev = self.position;
        self.perform_step_move_to(target, world_width, world_height, base_mul * speed_mul, energy_mul);
        const double disp_x = self.position.x - prev.x;
        const double disp_y = self.position.y - prev.y;
        const double disp_len = std::sqrt(disp_x*disp_x + disp_y*disp_y);
        const double arrival_threshold = std::max(0.2, self.get_current_step_distance() * 0.5);
        if (self.position.distance_to(target) <= arrival_threshold) {
            self.clear_wander_target();
            // 到达目标：返回 Success，让外层节点感知完成
            return Status::Success;
        }
        // 未到达：持续推进，返回 Running
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
        // 采样到新目标时不再输出 info 日志
    }
    // 统一移动逻辑：若采样失败执行后备移动；若采样成功则当帧立即移动到新目标
    const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
    const double speed_mul = bb_get_double(ctx.blackboard, "wander_speed_multiplier", 0.8);
    const double energy_mul = bb_get_double(ctx.blackboard, "wander_energy_multiplier", 0.6);
    const double base_energy_mul = bb_get_double(ctx.blackboard, "current_energy_multiplier", 1.0);
    if (!self.get_wander_target().has_value()) {
        std::uniform_real_distribution<> angle2(0.0, 2 * M_PI);
        const double a2 = angle2(rng_local);
        Position fallback{
            std::max(0.0, std::min(static_cast<double>(world_width), self.position.x + std::cos(a2) * self.movement_speed)),
            std::max(0.0, std::min(static_cast<double>(world_height), self.position.y + std::sin(a2) * self.movement_speed))
        };
        if (self.is_pregnant) {
            // 保留孕期速度计算，但不输出 info 日志
        }
        self.perform_step_move_to(fallback, world_width, world_height, base_mul * speed_mul, energy_mul * base_energy_mul);
        return Status::Running;
    } else {
        const Position target2 = self.get_wander_target().value();
        if (self.is_pregnant) {
            // 保留孕期速度计算，但不输出 info 日志
        }
        const Position prev2 = self.position;
        self.perform_step_move_to(target2, world_width, world_height, base_mul * speed_mul, energy_mul * base_energy_mul);
        const double disp_x2 = self.position.x - prev2.x;
        const double disp_y2 = self.position.y - prev2.y;
        const double disp_len2 = std::sqrt(disp_x2*disp_x2 + disp_y2*disp_y2);
        const double arrival_threshold2 = std::max(0.2, self.get_current_step_distance() * 0.5);
        if (self.position.distance_to(target2) <= arrival_threshold2) {
            self.clear_wander_target();
            // 到达新采样目标：返回 Success
            return Status::Success;
        }
        // 未到达：返回 Running
        return Status::Running;
    }
}

bt::Status ApproachOrMate(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;
    // 若交配计时器仍在进行，避免重复提交导致每 tick 停动
    if (self.mating_timer > 0) {
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[ApproachOrMate] Mating in progress (timer={}), skipping action",
            self.mating_timer);
        return Status::Failure;
    }
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
        
        return Status::Success;
    }

    self.set_mating_target(mate->position);
    self.set_current_target(self.get_mating_target());
    self.plan_path_to_target(*world, self.get_current_target());
    if (ctx.blackboard && self.get_current_target().has_value()) {
        ctx.blackboard->doubles["target_pos_x"] = self.get_current_target()->x;
        ctx.blackboard->doubles["target_pos_y"] = self.get_current_target()->y;
    }
    const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
    const double speed_mul = bb_get_double(ctx.blackboard, "mate_speed_multiplier", 1.0);
    const double energy_mul = bb_get_double(ctx.blackboard, "mate_energy_multiplier", 1.0);
    const double base_energy_mul = bb_get_double(ctx.blackboard, "current_energy_multiplier", 1.0);
    if (self.is_pregnant) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "[Move Mate] Pregnant speed: base_mul={:.2f} speed_mul={:.2f} final_mul={:.2f}",
            base_mul, speed_mul, base_mul * speed_mul);
    }
    self.perform_step_move_path(world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul * base_energy_mul);
    return Status::Running;
}

bt::Status EatNearbyThing(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    const std::string thing = params["thing"] ? params["thing"].as<std::string>() : (params["kind"] ? params["kind"].as<std::string>() : std::string("grass"));
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("eating_range");
    const double eat_range = bb_get_double(ctx.blackboard, range_key, self.eating_range);
    // Short-circuit: if chase already inside the hard stop range, fail so downstream nodes can act.
    const double stop_range = bb_get_double(ctx.blackboard, "eat_hard_stop_range", 0.0);
    // 调试：打印吃草进度 current/total 与范围（降噪为 DEBUG）
    {
        int total = bb_get_int(ctx.blackboard, "eat_grass_total_ticks", -1);
        int current = bb_get_int(ctx.blackboard, "eat_grass_current_ticks", 0);
        double pct = (total > 0) ? (static_cast<double>(current) / static_cast<double>(total) * 100.0) : 0.0;
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[EatNearbyThing] '{}' thing={} eat_range={:.1f} progress={}/{} ({:.0f}%)",
            self.species_name, thing, eat_range, current, total, pct);
    }
    if (eat_range <= 0.0) return Status::Failure;

    auto nearby_things = world->get_things_in_range(thing, self.position, eat_range);
    // 选择最近的草，并计算硬停止半径（仅当足够接近才停止移动）
    std::shared_ptr<ThingBase> nearest;
    double nearest_dist = std::numeric_limits<double>::max();
    for (const auto& t : nearby_things) {
        if (!t || !t->alive) continue;
        double d = self.position.distance_to(t->position);
        if (d < nearest_dist) { nearest_dist = d; nearest = t; }
    }
    if (nearest) {
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[EatNearbyThing] nearest_dist={:.2f} eat_range={:.1f} stop_range={:.1f}",
            nearest_dist, eat_range, stop_range);

        // 仅当足够接近时才提交吃草请求并停止移动；不够近则返回 Failure 以允许移动分支执行
    if (nearest_dist <= stop_range) {
            world->submit_interaction_request(AttemptToEatThingRequest{self.shared_from_this(), nearest});
            self.set_skip_movement(true);
            SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
                "[BT Action] {} eat within stop_range ({:.1f}); submit request, skip_movement=true",
                self.species_name, stop_range);
            // 成功提交进食请求：重置最近进食计时并施加轻微冷却
            if (ctx.blackboard) {
                ctx.blackboard->ints["ticks_since_last_meal"] = 0;
            }
            return Status::Success;
        } else {
            SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
                "[BT Action] {} see grass but outside stop_range ({:.1f}); return Failure to keep moving",
                self.species_name, stop_range);
            return Status::Failure;
        }
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

    if (range <= 0.0 || self.hunting_cooldown > 0 || desire <= 0.0) {
        return Status::Failure;
    }

    // Sticky hunting: remember the first valid target within range.
    std::shared_ptr<RaceBase> target_in_range;
    for (auto& wptr : self.get_cached_food_races_snapshot()) {
        auto r = wptr.lock();
        if (!r || !r->alive) continue;
        if (r.get() == &self) continue;
        if (r->species_name != race) continue;
        if (self.position.distance_to(r->position) <= range) {
            target_in_range = r;
            break;
        }
    }

    if (!target_in_range) {
        return Status::Failure;
    }

    auto& rng_local = world->get_thread_local_rng();
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
    const double rate = bb_get_double(ctx.blackboard, rate_key, self.hunting_success_rate);

    if (hunt_dist(rng_local) < rate * desire) {
        // 读取伤害参数：支持从黑板键覆盖或使用默认键 'attack_damage'
        const std::string dmg_key = params["damage_param"] ? params["damage_param"].as<std::string>() : std::string("attack_damage");
        const double damage = bb_get_double(ctx.blackboard, dmg_key, 10.0);
        world->submit_interaction_request(DamageRaceRequest{self.shared_from_this(), target_in_range, damage});
        self.start_hunting_cooldown();
        self.set_skip_movement(true);
        return Status::Success;
    }

    // Miss: keep the attack node active so the selector stays on P1.
    return Status::Running;
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

    const double stop_range = bb_get_double(ctx.blackboard, "eat_hard_stop_range", 0.0);
    if (stop_range > 0.0) {
        const double dist = self.position.distance_to(self.get_current_target().value());
        if (dist <= stop_range) {
            return Status::Failure;
        }
    }

    self.plan_path_to_target(*world, self.get_current_target());

    const double base_mul = bb_get_double(ctx.blackboard, "current_speed_multiplier", 1.0);
    const std::string speed_key = params["speed_multiplier_key"] ? params["speed_multiplier_key"].as<std::string>() : std::string("chase_speed_multiplier");
    const std::string energy_key = params["energy_multiplier_key"] ? params["energy_multiplier_key"].as<std::string>() : std::string("chase_energy_multiplier");
    const double speed_mul = bb_get_double(ctx.blackboard, speed_key, 1.0);
    const double energy_mul = bb_get_double(ctx.blackboard, energy_key, 1.0);
    const double base_energy_mul = bb_get_double(ctx.blackboard, "current_energy_multiplier", 1.0);
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("eating_range");
    const double eat_range = bb_get_double(ctx.blackboard, range_key, self.eating_range);
    const std::string move_mode = params["plan_path_move_mode"] ? params["plan_path_move_mode"].as<std::string>() : std::string("path");
    const double final_mul = base_mul * speed_mul;
    if (self.get_current_target().has_value()) {
        const Position tgt = self.get_current_target().value();
        const double dist = self.position.distance_to(tgt);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[PlanPath] mode={} substeps={} target=({:.1f},{:.1f}) dist={:.2f} eat_range={:.2f} base_mul={:.2f} speed_mul={:.2f} final_mul={:.2f}",
            move_mode,
            bb_get_int(ctx.blackboard, "chase_substeps_per_tick", 1),
            tgt.x, tgt.y, dist, eat_range,
            base_mul, speed_mul, final_mul);
    }
    // 支持每 tick 执行多步追逐以减少“逐帧小步”的视觉卡顿
    int substeps = bb_get_int(ctx.blackboard, "chase_substeps_per_tick", 1);
    if (substeps < 1) substeps = 1; if (substeps > 8) substeps = 8; // 上限保护，避免一次移动过多
    for (int i = 0; i < substeps; ++i) {
        if (move_mode == "direct") {
            // 直接朝目标推进，复刻游荡的平滑节奏
            if (self.get_current_target().has_value()) {
                self.perform_step_move_to(self.get_current_target().value(), world->config.world_width, world->config.world_height, final_mul, energy_mul);
            }
        } else {
            // 默认：沿路径点推进
            self.perform_step_move_path(world->config.world_width, world->config.world_height, final_mul, energy_mul);
        }
        // 若已到达并清空目标，提前结束本 tick 的后续子步
        if (!self.get_current_target().has_value()) break;
    }
    return Status::Running;
}

} // namespace behavior::actions