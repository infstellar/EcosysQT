#pragma once

#ifdef ECOSIM_ENABLE_UI_DEBUG

#include <string>

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
};

#endif // ECOSIM_ENABLE_UI_DEBUG
