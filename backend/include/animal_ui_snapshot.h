#pragma once

#ifdef ECOSIM_ENABLE_UI_DEBUG

#include <string>
#include <vector>
#include <optional>
#include "utils.h"

struct InteractionLogEntry {
    int timestamp = 0;
    std::string message;
    bool success = false;
};

struct AnimalUiSnapshot {
    std::string current_bt_action = "Idle";
    bool is_pregnant = false;
    int hunger_state = 1;
    int danger_nearby = 0;
    int perceived_mates = 0;
    int perceived_food = 0;
    int wander_current_ticks = 0;
    int wander_total_ticks = 50;
    // 生命值显示
    double hp_current = 0.0;
    double hp_max = 0.0;
    // 当前移速（每 tick 步长）
    double current_speed = 0.0;

    // 寻路调试：当前目标与规划路径（仅 UI Debug 模式）
    std::optional<Position> current_target;
    std::vector<Position> planned_path;

    std::vector<InteractionLogEntry> interaction_history;
};

#endif // ECOSIM_ENABLE_UI_DEBUG
