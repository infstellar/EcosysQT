#include "bt_actions.h"
#include "animal.h"
#include "ecosystem.h"
#include "interaction_requests.h"
#include "thing_base.h"
#include "race_factory.h"
#include "thing_factory.h"
#include "pathfinding.h"
#include <spdlog/spdlog.h>
#include "logger_once.hpp"
#include <algorithm>
#include <memory>
#include <limits>
#include <random>
#include <cmath>
#include <vector>
#include "tracy/Tracy.hpp"
#include "bt_keys.h"

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


bt::Status WanderAnywhere(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::Wander");
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
        const double base_mul = bb_get_double(ctx.blackboard, bt::keys::CurrentSpeedMultiplier, 1.0);
        const double speed_mul = bb_get_double(ctx.blackboard, bt::keys::WanderSpeedMultiplier, 0.8);
        const double energy_mul = bb_get_double(ctx.blackboard, bt::keys::WanderEnergyMultiplier, 0.6);
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
    const double base_mul = bb_get_double(ctx.blackboard, bt::keys::CurrentSpeedMultiplier, 1.0);
    const double speed_mul = bb_get_double(ctx.blackboard, bt::keys::WanderSpeedMultiplier, 0.8);
    const double energy_mul = bb_get_double(ctx.blackboard, bt::keys::WanderEnergyMultiplier, 0.6);
    const double base_energy_mul = bb_get_double(ctx.blackboard, bt::keys::CurrentEnergyMultiplier, 1.0);
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

bt::Status AttemptToMate(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::AttemptToMate");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive || self.get_skip_movement() || !ctx.blackboard) {
        return Status::Failure;
    }

    if (self.mating_timer > 0) {
        return Status::Failure;
    }

    const std::string range_key = params["mating_range_param"] ? params["mating_range_param"].as<std::string>() : std::string("mating_range");
    const double range = bb_get_double(ctx.blackboard, range_key, self.get_mating_range());
    if (range <= 0.0) {
        return Status::Failure;
    }

    auto candidates_in_range = world->get_races_in_range(self.species_name, self.position, range);
    std::shared_ptr<Animal> target_to_mate;
    for (const auto& race : candidates_in_range) {
        if (!race || race.get() == &self) continue;
        auto potential_mate = std::dynamic_pointer_cast<Animal>(race);
        if (potential_mate && potential_mate->sex == Sex::FEMALE && potential_mate->can_reproduce()) {
            target_to_mate = potential_mate;
            break;
        }
    }

    if (target_to_mate) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "Submitting mate request (attempt): male pos=({:.1f},{:.1f}), female pos=({:.1f},{:.1f}), range={:.2f}",
            self.position.x, self.position.y, target_to_mate->position.x, target_to_mate->position.y, range);

        world->submit_interaction_request(AttemptToMateRequest{target_to_mate, std::dynamic_pointer_cast<Animal>(self.shared_from_this())});

        // 成功帧跳过移动，统一与捕食/吃草的行为
        self.set_skip_movement(true);

        // 清理上下文目标与通用黑板缓存
        auto& bb = *ctx.blackboard;
        bb.doubles.erase(bt::keys::TargetPosX);
        bb.doubles.erase(bt::keys::TargetPosY);
        self.clear_current_target();
        self.clear_mating_target();
        return Status::Success;
    }

    return Status::Failure;
}

bt::Status EatTargetThing(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::EatThing");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive || !ctx.blackboard) {
        return Status::Failure;
    }

    // 0. 若本帧已由其他动作设置了跳过移动，则不能吃草（与捕食一致）
    if (self.get_skip_movement()) {
        return Status::Failure;
    }

    auto& bb = *ctx.blackboard;

    // 1. 目标锁定：必须存在黑板上的缓存目标位置
    if (bb.doubles.find(bt::keys::TargetPosX) == bb.doubles.end()) {
        return Status::Failure;
    }

    Position target_pos{
        bb_get_double(&bb, bt::keys::TargetPosX, 0.0),
        bb_get_double(&bb, bt::keys::TargetPosY, 0.0)
    };

    // 2. 停止范围读取：优先使用预计算的 eat_hard_stop_range；否则按 base*factor 计算
    double stop_range = bb_get_double(&bb, "eat_hard_stop_range", 0.0);
    if (stop_range <= 0.0) {
        const double base_range = bb_get_double(&bb, "stop_range_base_range", 1.0);
        const double factor = bb_get_double(&bb, "stop_range_factor", 0.5);
        stop_range = std::max(0.0, base_range * factor);
    }
    if (stop_range <= 0.0) {
        return Status::Failure;
    }

    // 3. 范围检查：未到达停止范围则返回 Failure，让追逐分支生效
    if (self.position.distance_to(target_pos) > stop_range) {
        return Status::Failure;
    }

    // 4. 本地“去幽灵化”验证：在小范围内寻找真实存在的目标实体
    const std::string thing = params["thing"] ? params["thing"].as<std::string>()
                            : (params["kind"] ? params["kind"].as<std::string>() : std::string("grass"));
    auto targets_in_range = world->find_nearest_things(self.position, std::vector<std::string>{thing}, 1, stop_range);
    if (targets_in_range.empty()) {
        // 目标可能已死亡或被其他实体消耗：清理黑板与当前目标
        bb.doubles.erase(bt::keys::TargetPosX);
        bb.doubles.erase(bt::keys::TargetPosY);
        self.clear_current_target();
        return Status::Failure;
    }
    auto target_to_eat = targets_in_range.front();

    // 5. 与进度装饰器协作：仅在第0帧提交吃草请求，最后一帧完成后清理目标
    const int current_ticks = bb_get_int(&bb, bt::keys::EatGrassCurrentTicks, 0);
    const int total_ticks = bb_get_int(&bb, bt::keys::EatGrassTotalTicks, 20);

    if (current_ticks == 0) {
        world->submit_interaction_request(AttemptToEatThingRequest{self.shared_from_this(), target_to_eat});
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "[BT Action] {} started eating (frame 0); submit request.",
            self.species_name);
    }

    if (current_ticks >= (total_ticks - 1)) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "[BT Action] {} finished eating (frame {}); clear target.",
            self.species_name, current_ticks);

        bb.doubles.erase(bt::keys::TargetPosX);
        bb.doubles.erase(bt::keys::TargetPosY);
        self.clear_current_target();

        // 重置最近进食计时
        bb.ints["ticks_since_last_meal"] = 0;
    }

    // 6. 处于吃草过程中的每一帧都跳过移动
    self.set_skip_movement(true);

    // 7. 返回 Success，让 ProgressLoopDecorator 推进/完成计时
    return Status::Success;
}

bt::Status HuntTargetRace(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::HuntRace");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive || self.get_skip_movement() || !ctx.blackboard) {
        return Status::Failure;
    }

    auto& bb = *ctx.blackboard;
    if (bb.doubles.find(bt::keys::TargetPosX) == bb.doubles.end()) {
        return Status::Failure;
    }

    Position target_pos{
        bb_get_double(&bb, bt::keys::TargetPosX, 0.0),
        bb_get_double(&bb, bt::keys::TargetPosY, 0.0)
    };

    const std::string race = params["race"] ? params["race"].as<std::string>() : (params["kind"] ? params["kind"].as<std::string>() : std::string("cow"));
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("hunting_range");
    const double range = bb_get_double(ctx.blackboard, range_key, self.hunting_range);
    const double desire = self.get_hunting_desire();

    if (range <= 0.0 || self.hunting_cooldown > 0 || desire <= 0.0) {
        return Status::Failure;
    }

    if (self.position.distance_to(target_pos) > range) {
        return Status::Failure;
    }

    auto targets_in_range = world->get_races_in_range(race, self.position, range);
    if (targets_in_range.empty()) {
        return Status::Failure;
    }

    auto target_to_attack = targets_in_range.front();

    auto& rng_local = world->get_thread_local_rng();
    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
    const std::string rate_key = params["success_rate_param"] ? params["success_rate_param"].as<std::string>() : std::string("hunting_success_rate");
    const double rate = bb_get_double(ctx.blackboard, rate_key, self.hunting_success_rate);

    if (hunt_dist(rng_local) < rate * desire) {
        const std::string dmg_key = params["damage_param"] ? params["damage_param"].as<std::string>() : std::string("attack_damage");
        // 支持攻击伤害区间：优先读取 <key>_min / <key>_max ，否则使用默认伤害
        const std::string dmg_min_key = dmg_key + std::string("_min");
        const std::string dmg_max_key = dmg_key + std::string("_max");

        const double dmg_min = bb_get_double(ctx.blackboard, dmg_min_key, bb_get_double(ctx.blackboard, "attack_damage_min", 0.0));
        const double dmg_max = bb_get_double(ctx.blackboard, dmg_max_key, bb_get_double(ctx.blackboard, "attack_damage_max", 0.0));

        double damage = 0.0;
        if (dmg_max > 0.0 && dmg_max >= dmg_min) {
            std::uniform_real_distribution<> dmg_dist(dmg_min, dmg_max);
            damage = dmg_dist(rng_local);
        } else {
            damage = bb_get_double(ctx.blackboard, dmg_key, 10.0);
        }

        DamageRaceRequest req{self.shared_from_this(), target_to_attack, damage};
        req.damage_min = (dmg_max > 0.0 && dmg_max >= dmg_min) ? dmg_min : bb_get_double(ctx.blackboard, dmg_key, 10.0);
        req.damage_max = (dmg_max > 0.0 && dmg_max >= dmg_min) ? dmg_max : req.damage_min;
        world->submit_interaction_request(req);

        self.start_hunting_cooldown();
        self.set_skip_movement(true);

        bb.doubles.erase(bt::keys::TargetPosX);
        bb.doubles.erase(bt::keys::TargetPosY);
        self.clear_current_target();

        return Status::Success;
    }

    return Status::Running;
}

bt::Status SelectTargetPoint(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::SelectTarget");
    (void)params;
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    std::vector<std::string> races_to_find;
    std::vector<std::string> things_to_find;

    for (const auto& food_name : self.food_types) {
        if (g_race_factory.is_registered(food_name)) { //
            races_to_find.push_back(food_name);
        } else if (g_thing_factory.is_registered(food_name)) { //
            things_to_find.push_back(food_name);
        }
    }

    std::shared_ptr<RaceBase> nearest_race_target;
    std::shared_ptr<ThingBase> nearest_thing_target;
    double min_distance = std::numeric_limits<double>::max();
    const double detect_range = self.get_food_detection_range();

    if (!races_to_find.empty()) {
        auto nearest_races = world->find_nearest_races(self.position, races_to_find, 1, detect_range);
        if (!nearest_races.empty() && nearest_races.front()) {
            nearest_race_target = nearest_races.front();
            min_distance = self.position.distance_to(nearest_race_target->position);
        }
    }

    if (!things_to_find.empty()) {
        auto nearest_things = world->find_nearest_things(self.position, things_to_find, 1, detect_range);
        if (!nearest_things.empty() && nearest_things.front()) {
            const double thing_distance = self.position.distance_to(nearest_things.front()->position);
            if (thing_distance < min_distance) {
                nearest_thing_target = nearest_things.front();
                min_distance = thing_distance;
                nearest_race_target.reset();
            }
        }
    }

    if (nearest_race_target) {
        self.set_current_target(nearest_race_target->position);
        if (ctx.blackboard) {
            ctx.blackboard->doubles[bt::keys::TargetPosX] = nearest_race_target->position.x;
            ctx.blackboard->doubles[bt::keys::TargetPosY] = nearest_race_target->position.y;
        }
        return Status::Success;
    }

    if (nearest_thing_target) {
        self.set_current_target(nearest_thing_target->position);
        if (ctx.blackboard) {
            ctx.blackboard->doubles[bt::keys::TargetPosX] = nearest_thing_target->position.x;
            ctx.blackboard->doubles[bt::keys::TargetPosY] = nearest_thing_target->position.y;
        }
        return Status::Success;
    }

    self.clear_current_target();
    self.clear_path();
    if (ctx.blackboard) {
        ctx.blackboard->doubles.erase(bt::keys::TargetPosX);
        ctx.blackboard->doubles.erase(bt::keys::TargetPosY);
    }
    return Status::Failure;
}

bt::Status SeekThingWithPath(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::SeekThingWithPath");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) {
        return Status::Failure;
    }
    if (self.get_skip_movement()) {
        return Status::Failure;
    }
    if (!ctx.blackboard) {
        return Status::Failure;
    }

    auto& bb = *ctx.blackboard;

    const std::string thing = params["thing"] ? params["thing"].as<std::string>()
                            : (params["kind"] ? params["kind"].as<std::string>() : std::string("grass"));
    if (thing.empty()) {
        return Status::Failure;
    }

    double detection_range = self.get_food_detection_range();
    if (params["detection_range_param"]) {
        detection_range = bb_get_double(&bb, params["detection_range_param"].as<std::string>(), detection_range);
    } else if (params["detection_range"]) {
        detection_range = std::max(0.0, params["detection_range"].as<double>());
    }

    double stop_range = bb_get_double(&bb, "eat_hard_stop_range", 0.0);
    if (stop_range <= 0.0) {
        const double base_range = bb_get_double(&bb, "stop_range_base_range", 1.0);
        const double factor = bb_get_double(&bb, "stop_range_factor", 0.5);
        stop_range = std::max(0.0, base_range * factor);
    }

    int search_interval = params["search_interval"] ? std::max(1, params["search_interval"].as<int>())
                        : bb_get_int(&bb, bt::keys::ForageSearchInterval, 300);
    if (search_interval < 1) {
        search_interval = 1;
    }
    bb.ints[bt::keys::ForageSearchInterval] = search_interval;

    const int last_search_tick = bb_get_int(&bb, bt::keys::ForageLastSearchTick, std::numeric_limits<int>::min());
    bool need_search = !self.get_current_target().has_value();
    if (!need_search) {
        const int ticks_elapsed = world->time_step - last_search_tick;
        if (ticks_elapsed < 0 || ticks_elapsed >= search_interval) {
            need_search = true;
        }
    }

    const WorldGrid& grid = world->world_grid();

    const auto target_valid = [&]() {
        if (!self.get_current_target().has_value()) {
            return false;
        }
        const Position target = self.get_current_target().value();
        const int tile_x = static_cast<int>(std::floor(target.x));
        const int tile_y = static_cast<int>(std::floor(target.y));
        if (!grid.is_valid_coord(tile_x, tile_y)) {
            return false;
        }
        const Tile& tile = grid.get_tile(tile_x, tile_y);
        for (ThingBase* thing_ptr : tile.things) {
            if (!thing_ptr || !thing_ptr->alive) {
                continue;
            }
            if (thing_ptr->species_name != thing) {
                continue;
            }
            const double dx = thing_ptr->position.x - target.x;
            const double dy = thing_ptr->position.y - target.y;
            if ((dx * dx + dy * dy) <= 0.25) {
                return true;
            }
        }
        return false;
    };

    if (!need_search) {
        const auto path_snapshot = self.get_planned_path_snapshot();
        if (path_snapshot.empty() && (!self.get_current_target().has_value() || (stop_range > 0.0 && self.position.distance_to(self.get_current_target().value()) > stop_range))) {
            need_search = true;
        } else if (!target_valid()) {
            need_search = true;
        }
    }

    if (need_search) {
        const auto& path_cfg = self.get_pathfinding_params();

        pathfinding::PathfindingSettings settings;
        settings.enable_smoothing = path_cfg.enable_smoothing;
        settings.min_traversal_cost = std::max(path_cfg.min_traversal_cost, 1e-6);
        if (path_cfg.max_iterations > 0) {
            settings.max_iterations = static_cast<std::size_t>(path_cfg.max_iterations);
        }
        if (!path_cfg.terrain_cost_overrides.empty()) {
            settings.terrain_cost_overrides = &path_cfg.terrain_cost_overrides;
        }

        const double absolute_budget = bb_get_double(&bb, "pathfinding_absolute_budget", 0.0);
        double budget_multiplier = bb_get_double(&bb, "pathfinding_budget_multiplier", path_cfg.budget_multiplier);
        if (budget_multiplier <= 0.0) {
            budget_multiplier = path_cfg.budget_multiplier;
        }
        if (budget_multiplier <= 0.0) {
            budget_multiplier = 1.0;
        }

        double max_cost = std::numeric_limits<double>::infinity();
        if (absolute_budget > 0.0 && std::isfinite(absolute_budget)) {
            max_cost = absolute_budget;
        } else if (detection_range > 0.0) {
            max_cost = detection_range * budget_multiplier;
        }

        pathfinding::GoalCondition goal_condition = [&, detection_range, thing](const pathfinding::GridPoint& gp, const WorldGrid& g) -> std::optional<Position> {
            if (!g.is_valid_coord(gp.x, gp.y)) {
                return std::nullopt;
            }
            const Tile& tile = g.get_tile(gp.x, gp.y);
            for (ThingBase* thing_ptr : tile.things) {
                if (!thing_ptr || !thing_ptr->alive) {
                    continue;
                }
                if (thing_ptr->species_name != thing) {
                    continue;
                }
                if (detection_range > 0.0) {
                    if (self.position.distance_to(thing_ptr->position) > detection_range) {
                        continue;
                    }
                }
                return thing_ptr->position;
            }
            return std::nullopt;
        };

        auto path_result = pathfinding::find_path_a_star_to_condition(
            self.position,
            goal_condition,
            grid,
            max_cost,
            settings);

        bb.ints[bt::keys::ForageLastSearchTick] = world->time_step;

        if (!path_result || path_result->path.empty()) {
            self.clear_current_target();
            self.clear_path();
            bb.doubles.erase(bt::keys::TargetPosX);
            bb.doubles.erase(bt::keys::TargetPosY);
            return Status::Failure;
        }

        const Position target_pos = path_result->path.back();
        self.set_current_target(target_pos);
        self.plan_path_to_target(path_result->path);

        bb.doubles[bt::keys::TargetPosX] = target_pos.x;
        bb.doubles[bt::keys::TargetPosY] = target_pos.y;
        bb.doubles[bt::keys::PathLastGoalX] = target_pos.x;
        bb.doubles[bt::keys::PathLastGoalY] = target_pos.y;
        bb.ints[bt::keys::PathLastPlanTick] = world->time_step;
    }

    if (!self.get_current_target().has_value()) {
        return Status::Failure;
    }

    const Position target_pos = self.get_current_target().value();
    if (stop_range > 0.0 && self.position.distance_to(target_pos) <= stop_range) {
        return Status::Success;
    }

    const double base_mul = bb_get_double(&bb, bt::keys::CurrentSpeedMultiplier, 1.0);
    const std::string speed_key = params["speed_multiplier_key"] ? params["speed_multiplier_key"].as<std::string>() : std::string("chase_speed_multiplier");
    const std::string energy_key = params["energy_multiplier_key"] ? params["energy_multiplier_key"].as<std::string>() : std::string("chase_energy_multiplier");
    const double speed_mul = bb_get_double(&bb, speed_key, 1.0);
    const double energy_mul = bb_get_double(&bb, energy_key, 1.0);
    const double base_energy_mul = bb_get_double(&bb, bt::keys::CurrentEnergyMultiplier, 1.0);

    int substeps = bb_get_int(&bb, "chase_substeps_per_tick", 1);
    if (substeps < 1) substeps = 1;
    if (substeps > 8) substeps = 8;

    for (int i = 0; i < substeps; ++i) {
        self.perform_step_move_path(world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul * base_energy_mul);
        if (!self.get_current_target().has_value()) {
            break;
        }
        if (stop_range > 0.0 && self.position.distance_to(self.get_current_target().value()) <= stop_range) {
            break;
        }
    }

    if (!self.get_current_target().has_value()) {
        return Status::Success;
    }
    if (stop_range > 0.0 && self.position.distance_to(self.get_current_target().value()) <= stop_range) {
        return Status::Success;
    }

    return Status::Running;
}

bt::Status SelectFleeDestination(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::SelectSmartFleeDest");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive || !ctx.blackboard) {
        return Status::Failure;
    }

    auto& bb = *ctx.blackboard;
    const std::string posx_key = params["pos_x_param"] ? params["pos_x_param"].as<std::string>() : std::string(bt::keys::ThreatPosX);
    const std::string posy_key = params["pos_y_param"] ? params["pos_y_param"].as<std::string>() : std::string(bt::keys::ThreatPosY);
    const std::string radius_key = params["radius_param"] ? params["radius_param"].as<std::string>() : std::string("flee_destination_radius");

    const double threat_x = bb_get_double(&bb, posx_key, self.position.x);
    const double threat_y = bb_get_double(&bb, posy_key, self.position.y);
    Position threat_pos{threat_x, threat_y};

    Position flee_dir{self.position.x - threat_pos.x, self.position.y - threat_pos.y};
    double len = std::sqrt(flee_dir.x * flee_dir.x + flee_dir.y * flee_dir.y);
    if (len < 1e-6) {
        auto& rng = world->get_thread_local_rng();
        std::uniform_real_distribution<> angle_dist(0.0, 2.0 * M_PI);
        const double random_angle = angle_dist(rng);
        flee_dir = {std::cos(random_angle), std::sin(random_angle)};
    } else {
        flee_dir.x /= len;
        flee_dir.y /= len;
    }

    double radius = bb_get_double(&bb, radius_key, 0.0);
    if (radius <= 0.0) {
        const double threat_default = self.get_threat_detection_range();
        const double th = bb_get_double(&bb, bt::keys::ThreatThreshold, threat_default);
        radius = std::max(threat_default, th * 1.5);
        SPDLOG_WARN_ONCE(spdlog::get("ecosim"),
            "Flee radius not configured for '{}' ; using dynamic fallback {:.1f}.",
            self.species_name, radius);
    }

    double min_angle_deg = params["min_angle_deg"] ? params["min_angle_deg"].as<double>() : -45.0;
    double max_angle_deg = params["max_angle_deg"] ? params["max_angle_deg"].as<double>() : 45.0;
    if (max_angle_deg < min_angle_deg) {
        std::swap(max_angle_deg, min_angle_deg);
    }
    const double min_angle_rad = min_angle_deg * (M_PI / 180.0);
    const double max_angle_rad = max_angle_deg * (M_PI / 180.0);
    const bool flank_mode = (min_angle_deg >= 45.0 && max_angle_deg >= min_angle_deg);

    auto& rng = world->get_thread_local_rng();
    std::uniform_real_distribution<> dist_radius(radius * 0.7, radius);
    std::uniform_real_distribution<> dist_angle(min_angle_rad, max_angle_rad);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::vector<Position> candidates;
    candidates.reserve(7);
    for (int i = 0; i < 7; ++i) {
        double angle_offset = dist_angle(rng);
        if (flank_mode) {
            if (side_dist(rng) == 0) {
                angle_offset = -angle_offset;
            }
        }

        const double sample_radius = dist_radius(rng);
        const double ca = std::cos(angle_offset);
        const double sa = std::sin(angle_offset);
        Position final_dir{
            flee_dir.x * ca - flee_dir.y * sa,
            flee_dir.x * sa + flee_dir.y * ca
        };

        Position candidate{
            self.position.x + final_dir.x * sample_radius,
            self.position.y + final_dir.y * sample_radius
        };

        candidate.x = std::clamp(candidate.x, 0.0, static_cast<double>(world->config.world_width));
        candidate.y = std::clamp(candidate.y, 0.0, static_cast<double>(world->config.world_height));
        candidates.push_back(candidate);
    }

    const auto& path_cfg = self.get_pathfinding_params();
    const auto estimate_cost = [&](TerrainType terrain) -> double {
        const auto& overrides = path_cfg.terrain_cost_overrides;
        const auto it = overrides.find(terrain);
        if (it != overrides.end()) {
            return it->second;
        }
        switch (terrain) {
            case TerrainType::LAND:          return 1.0;
            case TerrainType::SAND:          return 2.5;
            case TerrainType::INLAND_SAND:   return 3.5;
            case TerrainType::HILLS:         return 3.5;
            case TerrainType::SHALLOW_RIVER: return 10.0;
            case TerrainType::MOUNTAIN:
            case TerrainType::DEEP_RIVER:
            case TerrainType::WATER:
            case TerrainType::SHALLOW_OCEAN:
            case TerrainType::DEEP_OCEAN:
            default:
                return std::numeric_limits<double>::infinity();
        }
    };

    Position best_target{};
    double max_dist_sq = -1.0;
    bool found_target = false;

    for (const auto& target : candidates) {
        const int tile_x = static_cast<int>(std::floor(target.x));
        const int tile_y = static_cast<int>(std::floor(target.y));
        if (!world->world_grid().is_valid_coord(tile_x, tile_y)) {
            continue;
        }

        const Tile& tile = world->world_grid().get_tile(tile_x, tile_y);
        const double cost = estimate_cost(tile.terrain);
        if (!std::isfinite(cost) || cost <= 0.0) {
            continue;
        }

        const double dx = target.x - self.position.x;
        const double dy = target.y - self.position.y;
        const double dist_sq = dx * dx + dy * dy;
        if (dist_sq > max_dist_sq) {
            max_dist_sq = dist_sq;
            best_target = target;
            found_target = true;
        }
    }

    if (!found_target) {
        return Status::Failure;
    }

    bb.doubles[bt::keys::TargetPosX] = best_target.x;
    bb.doubles[bt::keys::TargetPosY] = best_target.y;
    self.set_current_target(best_target);
    self.clear_path();
    bb.ints[bt::keys::ForageLastSearchTick] = std::numeric_limits<int>::min();
    bb.ints.erase("mate_target_id");
    self.clear_mating_target();

    return Status::Success;
}

bt::Status PlanPathToTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params) {
    ZoneScopedN("BT::Action::PlanPath");
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) return Status::Failure;
    if (self.get_skip_movement()) return Status::Failure;

    if (!self.get_current_target().has_value()) return Status::Failure;

    const Position target_pos = self.get_current_target().value();
    const PathfindingParams& path_cfg = self.get_pathfinding_params();

    const double stop_range = bb_get_double(ctx.blackboard, "eat_hard_stop_range", 0.0);
    if (stop_range > 0.0) {
        const double dist = self.position.distance_to(target_pos);
        if (dist <= stop_range) {
            return Status::Failure;
        }
    }

    const std::string move_mode = params["plan_path_move_mode"] ? params["plan_path_move_mode"].as<std::string>() : std::string("path");

    if (move_mode == "direct") {
        self.plan_path_to_target(*world, self.get_current_target());
    } else {
        std::vector<Position> path_snapshot = self.get_planned_path_snapshot();
        constexpr double kTargetTolerance = 0.5;
        const int replan_interval = std::max(0, bb_get_int(ctx.blackboard, std::string(bt::keys::PathReplanInterval), path_cfg.replan_interval));
        bool need_replan = path_snapshot.empty();
        if (!need_replan) {
            const Position& planned_goal = path_snapshot.back();
            if (planned_goal.distance_to(target_pos) > kTargetTolerance) {
                need_replan = true;
            }
        }
        if (!need_replan) {
            if (replan_interval <= 1) {
                need_replan = true;
            } else {
                const int last_plan_tick = bb_get_int(ctx.blackboard, std::string(bt::keys::PathLastPlanTick), std::numeric_limits<int>::min());
                if (last_plan_tick == std::numeric_limits<int>::min()) {
                    need_replan = true;
                } else {
                    const int ticks_since_last_plan = world->time_step - last_plan_tick;
                    if (ticks_since_last_plan < 0 || ticks_since_last_plan >= replan_interval) {
                        need_replan = true;
                    }
                }
            }
        }
        if (need_replan) {
            const double straight_line_dist = self.position.distance_to(target_pos);
            const double absolute_budget = bb_get_double(ctx.blackboard, "pathfinding_absolute_budget", 0.0);
            double max_cost = std::numeric_limits<double>::infinity();
            if (absolute_budget > 0.0 && std::isfinite(absolute_budget)) {
                max_cost = absolute_budget;
            } else {
                const double budget_multiplier = bb_get_double(ctx.blackboard, "pathfinding_budget_multiplier", path_cfg.budget_multiplier);
                max_cost = (budget_multiplier > 0.0)
                    ? straight_line_dist * budget_multiplier
                    : std::numeric_limits<double>::infinity();
            }

            pathfinding::PathfindingSettings settings;
            settings.enable_smoothing = path_cfg.enable_smoothing;
            settings.min_traversal_cost = std::max(path_cfg.min_traversal_cost, 1e-6);
            const int max_iterations = std::max(0, bb_get_int(ctx.blackboard, "pathfinding_max_iterations", path_cfg.max_iterations));
            if (max_iterations > 0) {
                settings.max_iterations = static_cast<std::size_t>(max_iterations);
            }
            if (!path_cfg.terrain_cost_overrides.empty()) {
                settings.terrain_cost_overrides = &path_cfg.terrain_cost_overrides;
            }

            auto path_result = pathfinding::find_path_a_star(self.position, target_pos, world->world_grid(), max_cost, settings);
            if (!path_result.has_value() || path_result->path.empty()) {
                SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                    "[Pathfinding] '{}' failed pathfind to ({:.1f},{:.1f}); budget={:.2f} straight={:.2f}",
                    self.species_name, target_pos.x, target_pos.y, max_cost, straight_line_dist);
                self.clear_current_target();
                self.clear_path();
                if (ctx.blackboard) {
                    ctx.blackboard->ints[bt::keys::PathLastPlanTick] = world->time_step;
                    ctx.blackboard->doubles[bt::keys::PathLastGoalX] = target_pos.x;
                    ctx.blackboard->doubles[bt::keys::PathLastGoalY] = target_pos.y;
                }
                return Status::Failure;
            }

            self.plan_path_to_target(path_result->path);
            if (ctx.blackboard) {
                ctx.blackboard->ints[bt::keys::PathLastPlanTick] = world->time_step;
                ctx.blackboard->doubles[bt::keys::PathLastGoalX] = target_pos.x;
                ctx.blackboard->doubles[bt::keys::PathLastGoalY] = target_pos.y;
            }
        }
    }

    const double base_mul = bb_get_double(ctx.blackboard, bt::keys::CurrentSpeedMultiplier, 1.0);
    const std::string speed_key = params["speed_multiplier_key"] ? params["speed_multiplier_key"].as<std::string>() : std::string("chase_speed_multiplier");
    const std::string energy_key = params["energy_multiplier_key"] ? params["energy_multiplier_key"].as<std::string>() : std::string("chase_energy_multiplier");
    const double speed_mul = bb_get_double(ctx.blackboard, speed_key, 1.0);
    const double energy_mul = bb_get_double(ctx.blackboard, energy_key, 1.0);
    const double base_energy_mul = bb_get_double(ctx.blackboard, bt::keys::CurrentEnergyMultiplier, 1.0);
    const std::string range_key = params["range_param"] ? params["range_param"].as<std::string>() : std::string("eating_range");
    const double eat_range = bb_get_double(ctx.blackboard, range_key, self.eating_range);
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