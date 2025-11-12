#ifndef UTILS_H
#define UTILS_H
#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <optional>
#include <map>
#include <Eigen/Dense>
#include <yaml-cpp/yaml.h>

// 前向声明
class RaceBase;
class ThingBase;
class WorldGrid;

// 交互请求类型已迁移至 interaction.h

// 表示2D空间中坐标的位置结构体
struct Position {
    double x;
    double y;
    // 计算到另一个位置的欧几里得距离
    double distance_to(const Position& other) const {
        return std::sqrt((x - other.x)*(x - other.x) + (y - other.y)*(y - other.y));
    }
};

// 用于模拟和前端的生态系统状态快照
struct EcosystemStateData {
    int world_width;
    int world_height;
    std::map<std::string, std::vector<std::shared_ptr<RaceBase>>> race_lists;
    std::map<std::string, std::vector<std::shared_ptr<ThingBase>>> thing_lists;
    int time_step;
    int current_day;
    int current_quadrum;
    int current_year;
    int current_hour;
    int current_minute;
    std::string current_quadrum_name;
    Eigen::MatrixXd grass_positions_array; // 对应Python中的numpy数组
    std::vector<std::shared_ptr<ThingBase>> alive_grass_objects;
    // 新增：后端模拟TPS（每秒tick数）
    double current_tps;
    // 新增：世界网格指针（只读引用）
    const WorldGrid* world_grid = nullptr;
    
    std::string toYaml() const;
    void fromYaml(const YAML::Node& node);
    
};

#endif // UTILS_H