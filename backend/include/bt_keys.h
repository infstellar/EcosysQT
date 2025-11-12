// 统一行为树黑板键名常量，减少魔法字符串与拼写错误
#pragma once

namespace bt::keys {
    // 通用移动目标
    constexpr const char* TargetPosX = "target_pos_x";
    constexpr const char* TargetPosY = "target_pos_y";

    // 当前速度/能耗倍率
    constexpr const char* CurrentSpeedMultiplier = "current_speed_multiplier";
    constexpr const char* CurrentEnergyMultiplier = "current_energy_multiplier";

    // 游荡参数
    constexpr const char* WanderSpeedMultiplier = "wander_speed_multiplier";
    constexpr const char* WanderEnergyMultiplier = "wander_energy_multiplier";
    constexpr const char* WanderTotalTicks = "wander_total_ticks";
    constexpr const char* WanderCurrentTicks = "wander_current_ticks";

    // 交配/欲望参数
    constexpr const char* MatingTimerTicks = "mating_timer_ticks";
    constexpr const char* MatingDesireProbability = "mating_desire_probability";
    constexpr const char* MatingRange = "mating_range";

    // 威胁/逃逸参数
    constexpr const char* DangerNearby = "danger_nearby";
    constexpr const char* FleeModeCooldownTicks = "flee_mode_cooldown_ticks";
    constexpr const char* ThreatDistance = "threat_distance";
    constexpr const char* ThreatThreshold = "threat_threshold";
    constexpr const char* ThreatPosX = "threat_pos_x";
    constexpr const char* ThreatPosY = "threat_pos_y";
    constexpr const char* HpRatio = "hp_ratio";

    // 吃草进度（用于 UI 或循环装饰器）
    constexpr const char* EatGrassTotalTicks = "eat_grass_total_ticks";
    constexpr const char* EatGrassCurrentTicks = "eat_grass_current_ticks";

    // 寻路元数据
    constexpr const char* PathLastPlanTick = "path_last_plan_tick";
    constexpr const char* PathLastGoalX = "path_last_goal_x";
    constexpr const char* PathLastGoalY = "path_last_goal_y";
    constexpr const char* PathReplanInterval = "path_replan_interval";

    // 觅食目标搜索节流
    constexpr const char* ForageLastSearchTick = "forage_last_search_tick";
    constexpr const char* ForageSearchInterval = "forage_search_interval";
}