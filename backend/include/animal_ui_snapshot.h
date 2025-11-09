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
};

#endif // ECOSIM_ENABLE_UI_DEBUG
