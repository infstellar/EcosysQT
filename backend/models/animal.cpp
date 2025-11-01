/*
物种数据模型 - Animal 基类实现
定义生态系统中的动物基类，继承自Species并添加智能移动
*/

#include "species.h"
#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <cmath>

// --- Animal ---
// 动物基类 - 继承自Species并添加智能移动
Animal::Animal(Position pos,
               double energy,
               int max_age,
               double reproduction_energy_cost,
               double movement_speed,
               int energy_consumption,
               double hunting_range,
               double hunting_success_rate,
               double detection_range,
               std::vector<std::string> food_types,
               int hunting_cooldown_duration,
               int min_reproduction_age,
               int base_reproduction_cooldown,
               double eating_range)
    : Species(pos, energy, max_age, reproduction_energy_cost),
      movement_speed(movement_speed),
      energy_consumption(energy_consumption),
      hunting_range(hunting_range),
      hunting_success_rate(hunting_success_rate),
      detection_range(detection_range),
      food_types(std::move(food_types)),
      hunting_cooldown(0),
      hunting_cooldown_duration(hunting_cooldown_duration),
      min_reproduction_age(min_reproduction_age),
      base_reproduction_cooldown(base_reproduction_cooldown),
      eating_range(eating_range) {}

std::optional<Position> Animal::find_nearest_food(const EcosystemState& ecosystem_state) {
    // 寻找最近的食物源
    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();

    for (const auto& food_type : food_types) {
        auto it = ecosystem_state.get_ecosystem_state().species_lists.find(food_type); // 查找食物类型
        if (it == ecosystem_state.get_ecosystem_state().species_lists.end()) continue; // 未找到食物类型
        
        const auto& food_list = it->second; // 获取食物列表
        for (const auto& food : food_list) { // 遍历食物列表
            if (food->alive) {
                double distance = position.distance_to(food->position);
                if (distance <= detection_range && distance < min_distance) { // 检测范围内且更近
                    min_distance = distance;
                    nearest_food = food->position;
                }
            }
        }
    }
    return nearest_food;
}

void Animal::move_towards_target(const Position& target_position, int world_width, int world_height) {
    // 朝目标位置移动
    if (!alive) return;
    double dx = target_position.x - position.x;
    double dy = target_position.y - position.y;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance > 0) {
        dx = (dx / distance) * movement_speed;
        dy = (dy / distance) * movement_speed;
        // 更新位置，确保不超出边界
        position.x = std::max(0.0, std::min((double)world_width, position.x + dx));
        position.y = std::max(0.0, std::min((double)world_height, position.y + dy));
    }
}

void Animal::intelligent_move(const EcosystemState& ecosystem_state) {
    // 智能移动 - 寻找食物或随机移动
    if (!alive) return;
    if (hunting_cooldown > 0) {
        hunting_cooldown -= 1;
        return;
    }
    
    int world_width = ecosystem_state.config.world_width;
    int world_height = ecosystem_state.config.world_height;
    
    // 寻找最近的食物
    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();
    
    // 遍历所有食物类型
    for (const auto& food_type : food_types) {
        // 使用通用查询接口获取探测范围内的食物
        auto food_in_range = ecosystem_state.get_species_in_range(food_type, position, detection_range);
        
        // 从返回的食物中找到最近的一个
        for (const auto& food : food_in_range) {
            double distance = position.distance_to(food->position);
            if (distance < min_distance) {
                min_distance = distance;
                nearest_food = food->position;
            }
        }
    }
    
    // 移动到最近的食物或随机移动
    if (nearest_food.has_value()) {
        move_towards_target(nearest_food.value(), world_width, world_height);
    } else {
        move_randomly(world_width, world_height, movement_speed);
    }
}

void Animal::start_hunting_cooldown() {
    // 开始狩猎冷却 - 动物将保持静止一段时间
    hunting_cooldown = hunting_cooldown_duration;
}

bool Animal::can_reproduce() const {
    // 动物通用繁殖判断：基础条件 + 年龄门槛
    return Species::can_reproduce() && age > min_reproduction_age;
}

void Animal::start_reproduction_cooldown() {
    // 开始繁殖冷却：使用基础冷却值
    reproduction_cooldown = base_reproduction_cooldown;
}

std::unique_ptr<Species> Animal::reproduce(const EcosystemState& ecosystem_state) {
    // 统一的动物繁殖逻辑
    Species::reproduce(ecosystem_state);
    if (!can_reproduce()) return nullptr;

    // 扣除能量并开始冷却
    energy -= reproduction_energy_cost;
    start_reproduction_cooldown();

    // 计算子代位置（在世界边界内的随机偏移）
    const auto& ecosystem_data = ecosystem_state.get_ecosystem_state();
    int world_width = ecosystem_data.world_width;
    int world_height = ecosystem_data.world_height;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    double radius = reproduction_spawn_radius();
    std::uniform_real_distribution<> dist_x(-radius, radius);
    std::uniform_real_distribution<> dist_y(-radius, radius);

    double new_x = std::max(0.0, std::min((double)world_width, position.x + dist_x(gen)));
    double new_y = std::max(0.0, std::min((double)world_height, position.y + dist_y(gen)));
    Position new_position{new_x, new_y};

    // 使用物种键创建子代（键来自 species_name）
    return g_species_factory.create(species_name, new_position);
}