/*
动物行为模块实现 - 行为树构建
按既定方案构建三大分支：
1) 交配（雄性触发，意愿概率，近距离提交交互，远距离锁定目标并路径前往）
2) 觅食/捕食（近场吃草/狩猎，失败则远处目标选择与路径）
3) 游荡（采样游荡目标，平滑到达与模式标记）
迁移阶段说明：在 use_bt=true 时，行为树的 Action 将直接执行一步移动并进行能量结算；apply 作为兼容层收口。
*/

#include "animal_behavior.h"
#include "behavior_tree.h"
#include "animal.h"
#include "ecosystem.h"
#include "interaction_requests.h"
#include "thing_base.h"
#include <random>
#include <cmath>

namespace behavior {

using namespace bt;

// 主节点：吃草动作（内联 Action），在近场范围内提交吃草交互

std::unique_ptr<BehaviorTree> build_tree_for_animal(Animal& self) {
    // 根：序列(UpdateState -> Succeeder(PrioritySelector(交配/觅食/游荡)) -> Finalize)
    // 使用 Succeeder 包裹优先选择器，确保无论其返回 Running/Success 都继续执行 Finalize，
    // 避免 Running 阻断导致 skip_movement 等临时标记无法在本 tick 清理。
    auto root_seq = std::make_shared<Sequence>();
    auto root_selector = std::make_shared<PrioritySelector>();

    // --- 通用：UpdateStateTick ---
    auto act_update = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        // 注：skip_movement 仅由本 tick 的具体动作设置（交配、分娩、近场吃草/捕食），
        // 不再从黑板读取跨 tick 标记，避免装饰器未被执行时残留导致卡住。

        // 饥饿与状态系数
        self.update_hunger_state();
        self.adjust_stats_by_state();

        // 求偶意图锁定与释放
        if (self.mating_intent_lock_ticks > 0) {
            self.mating_intent_lock_ticks -= 1;
        } else {
            if (self.mating_target.has_value() && self.hunger_state == HungerState::STARVING) {
                self.mating_target.reset();
            }
        }

        // 进行中的交配计时器
        if (self.mating_timer > 0) {
            self.mating_timer -= 1;
            self.skip_movement = true; // 交配中本 tick 不移动
        }

        // 怀孕推进与分娩提交
        if (self.is_pregnant) {
            self.pregnancy_timer -= 1;
            if (self.pregnancy_timer <= 0) {
                self.is_pregnant = false;
                // 生成分娩位置候选
                auto& rng_local = world->get_thread_local_rng();
                std::uniform_real_distribution<> dist_angle(0.0, 2 * M_PI);
                std::uniform_real_distribution<> dist_radius(std::max(0.2, self.movement_speed * 0.2), std::max(0.5, self.movement_speed * 2.5));
                const double angle = dist_angle(rng_local);
                const double distance = dist_radius(rng_local);
                Position spawn_candidate{
                    std::max(0.0, std::min(static_cast<double>(world->config.world_width), self.position.x + std::cos(angle) * distance)),
                    std::max(0.0, std::min(static_cast<double>(world->config.world_height), self.position.y + std::sin(angle) * distance))
                };
                self.pending_spawn_position = spawn_candidate;
                // 提交分娩请求并进入产后冷却
                world->submit_interaction_request(AttemptToReproduceRaceRequest{self.shared_from_this()});
                self.start_reproduction_cooldown();
                self.skip_movement = true; // 分娩本 tick 不移动
            }
        }

        // 冷却推进
        if (self.hunting_cooldown > 0) {
            self.hunting_cooldown -= 1;
        }

        return Status::Success;
    });

    // --- 交配序列：条件 -> 追配偶/提交交互 ---
    auto seq_mate = std::make_shared<Sequence>();
    seq_mate->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.sex == Sex::MALE && self.can_reproduce();
    }));
    seq_mate->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;

        // 求偶意愿概率
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> desire_dist(0.0, 1.0);
        if (desire_dist(rng_local) >= self.mating_desire_probability) {
            return Status::Failure; // 未命中意愿，交由后续分支
        }

        auto mate_opt = self.find_available_mate(*world);
        if (!mate_opt.has_value()) {
            return Status::Failure;
        }
        auto mate = mate_opt.value();
        if (!mate || !mate->alive) {
            return Status::Failure;
        }

        if (self.position.distance_to(mate->position) <= self.mating_range) {
            // 在范围内，提交交配请求并跳过移动
            world->submit_interaction_request(AttemptToMateRequest{mate, std::dynamic_pointer_cast<Animal>(self.shared_from_this())});
            self.skip_movement = true;
            return Status::Running;
        } else {
            // 未到范围内：锁定交配意图并前往配偶位置
            self.mating_target = mate->position;
            self.mating_intent_lock_ticks = self.mating_intent_lock_duration;
            self.current_target = self.mating_target;
            self.plan_path_to_target(*world, self.current_target);
            // 直接执行一步移动并结算能量
            self.current_step_distance = self.step_distance_per_tick;
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            self.move_to_target_point(ww, wh);
            self.energy -= self.energy_consumption;
            if (self.energy <= 0.0) {
                self.die_from_starvation();
            }
            return Status::Running;
        }
    }));

    // --- 觅食/捕食序列：条件 -> 近场吃草(进度) -> 远处目标与移动/捕食 ---
    auto seq_forage = std::make_shared<Sequence>();
    seq_forage->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.hunger_state != HungerState::SATISFIED && !self.food_types.empty();
    }));
    // 近场吃草：由进度装饰器控制持续时间（仅当主食为 grass 时）
    // 并通过 Selector 保障非草食动物（如老虎）不会被该动作阻塞
    auto sel_forage_inner = std::make_shared<Selector>();
    {
        const int default_eat_ticks = 300; // 可配置：行为树编辑器参数 ${total_ticks}
        auto eat_action = std::make_shared<Action>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return Status::Failure;
            if (self.skip_movement) return Status::Failure;
            if (self.food_types.empty() || self.hunger_state == HungerState::SATISFIED) {
                return Status::Failure;
            }
            const std::string& primary_food = self.food_types.front();
            if (primary_food != std::string("grass")) {
                return Status::Failure;
            }
            if (self.eating_range <= 0.0) {
                return Status::Failure;
            }
            auto nearby_things = world->get_things_in_range("grass", self.position, self.eating_range);
            for (const auto& thing_ptr : nearby_things) {
                if (!thing_ptr || !thing_ptr->alive) continue;
                world->submit_interaction_request(AttemptToEatThingRequest{self.shared_from_this(), thing_ptr});
                self.skip_movement = true; // 本 tick 不移动
                // 进度相关：若存在黑板，保持 current/total 用于显示
                (void)ctx.blackboard;
                return Status::Success;
            }
            return Status::Failure;
        });
        auto eat_with_progress = std::make_shared<ProgressDecorator>(eat_action,
            "eat_grass_total_ticks",
            "eat_grass_current_ticks",
            default_eat_ticks);
        // 仅在主食为 grass 时执行进度吃草，否则跳过以尝试后续分支
        auto grass_only_seq = std::make_shared<Sequence>();
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext&){
            if (self.food_types.empty()) return false;
            const std::string& primary_food = self.food_types.front();
            return primary_food == std::string("grass");
        }));
        // 仅当近场确有草可吃时才进入进度阶段，避免在无草时被阻塞
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return false;
            if (self.eating_range <= 0.0) return false;
            auto nearby_things = world->get_things_in_range("grass", self.position, self.eating_range);
            return !nearby_things.empty();
        }));
        grass_only_seq->add_child(eat_with_progress);
        sel_forage_inner->add_child(grass_only_seq);
    }

    // 远处移动/捕食与兜底逻辑
    auto action_hunt_or_move = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;

        if (self.food_types.empty() || self.hunger_state == HungerState::SATISFIED) {
            return Status::Failure;
        }
        const std::string& primary_food = self.food_types.front();

        // 捕食：对牛的狩猎（与现有逻辑一致）
        if (primary_food == std::string("cow") && self.hunting_range > 0.0 && self.hunting_cooldown <= 0) {
            const double desire = self.get_hunting_desire();
            if (desire > 0.0) {
                auto& rng_local = world->get_thread_local_rng();
                std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
                if (hunt_dist(rng_local) < self.hunting_success_rate * desire) {
                    auto nearby_races = world->get_nearby_races_broad(self.position, self.hunting_range);
                    for (const auto& race : nearby_races) {
                        if (!race || !race->alive) continue;
                        if (race.get() == &self) continue;
                        if (race->species_name != "cow") continue;
                        if (self.position.distance_to(race->position) > self.hunting_range) continue;
                        world->submit_interaction_request(AttemptToEatRaceRequest{self.shared_from_this(), race});
                        self.start_hunting_cooldown();
                        self.skip_movement = true; // 近场捕食本 tick 不移动
                        break; // 单次狩猎
                    }
                }
            }
        }

        // 远处食物：选择最近食物为目标并前往；若无目标，交由游荡分支
        self.select_target_point(*world);
        if (self.current_target.has_value()) {
            self.plan_path_to_target(*world, self.current_target);
            // 直接执行一步移动并结算能量
            self.current_step_distance = self.step_distance_per_tick;
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            self.move_to_target_point(ww, wh);
            self.energy -= self.energy_consumption;
            if (self.energy <= 0.0) {
                self.die_from_starvation();
            }
            return Status::Running;
        }

        // 无目标：返回 Failure，让 Selector 切到游荡
        return Status::Failure;
    });
    sel_forage_inner->add_child(action_hunt_or_move);

    // 将内层 Selector 作为觅食序列的第二个子节点
    seq_forage->add_child(sel_forage_inner);

    // --- 游荡行为：无条件行动 ---
    auto act_wander = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure; // 本 tick 不应移动

        const int world_width = world->config.world_width;
        const int world_height = world->config.world_height;

        // 若已有游荡目标，直接推进一步
        if (self.wander_target.has_value()) {
            self.current_step_distance = self.step_distance_per_tick;
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            const Position target = self.wander_target.value();
            self.move_towards_target(target, ww, wh);
            // 满足状态下步长较小，降低到达阈值，避免“未动就判定到达”
            const double arrival_threshold = std::max(0.2, self.current_step_distance * 0.5);
            if (self.position.distance_to(target) <= arrival_threshold) {
                self.wander_target.reset();
            }
            self.energy -= self.energy_consumption;
            if (self.energy <= 0.0) {
                self.die_from_starvation();
            }
            return Status::Running;
        }

        // 采样新游荡目标（与现有 decide 中一致）
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> angle_dist(0.0, 2 * M_PI);
        std::uniform_real_distribution<> unit01(0.0, 1.0);

        for (int tries = 0; tries < 6 && !self.wander_target.has_value(); ++tries) {
            const double angle = angle_dist(rng_local);
            const double r = std::max(self.movement_speed, std::sqrt(unit01(rng_local)) * self.wander_radius);
            Position candidate{
                self.position.x + std::cos(angle) * r,
                self.position.y + std::sin(angle) * r
            };
            candidate.x = std::max(0.0, std::min(static_cast<double>(world_width), candidate.x));
            candidate.y = std::max(0.0, std::min(static_cast<double>(world_height), candidate.y));
            if (self.position.distance_to(candidate) < 1e-6) continue;
            self.wander_target = candidate;
        }
        if (!self.wander_target.has_value()) {
            // 兜底：若未选中合法目标，执行一次小幅随机移动
            std::uniform_real_distribution<> angle2(0.0, 2 * M_PI);
            const double a2 = angle2(rng_local);
            Position fallback{
                std::max(0.0, std::min(static_cast<double>(world_width), self.position.x + std::cos(a2) * self.movement_speed)),
                std::max(0.0, std::min(static_cast<double>(world_height), self.position.y + std::sin(a2) * self.movement_speed))
            };
            self.wander_target = fallback;
        }

        // 推进一步并结算能量
        self.current_step_distance = self.step_distance_per_tick;
        const int ww2 = world->config.world_width;
        const int wh2 = world->config.world_height;
        const Position target2 = self.wander_target.value();
        self.move_towards_target(target2, ww2, wh2);
        const double arrival_threshold2 = std::max(0.2, self.current_step_distance * 0.5);
        if (self.position.distance_to(target2) <= arrival_threshold2) {
            self.wander_target.reset();
        }
        self.energy -= self.energy_consumption;
        if (self.energy <= 0.0) {
            self.die_from_starvation();
        }
        return Status::Running;
    });

    root_selector->add_child(seq_mate);
    root_selector->add_child(seq_forage);
    root_selector->add_child(act_wander);

    // --- 通用：FinalizeTick ---
    auto act_finalize = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        (void)world;
        if (!self.alive) return Status::Failure;

        // 统一清理：当本 tick 被请求占用或交配/分娩进行时，清理临时目标与路径
        if (self.skip_movement) {
            self.current_target.reset();
            self.planned_path.clear();
            self.planned_path_index = 0;
            self.wander_target.reset();
            self.mating_target.reset();
        }
        // 本 tick 结束，重置仅当 tick 内使用的标记
        self.skip_movement = false;
        return Status::Success;
    });

    // 确保优先选择器的返回不会阻断 Finalize
    auto selector_succeeder = std::make_shared<Succeeder>(root_selector);

    root_seq->add_child(act_update);
    root_seq->add_child(selector_succeeder);
    root_seq->add_child(act_finalize);

    return std::make_unique<BehaviorTree>(root_seq);
}

} // namespace behavior