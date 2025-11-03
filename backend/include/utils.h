#ifndef UTILS_H
#define UTILS_H
#define _USE_MATH_DEFINES // 把这行加在 #include <cmath> 之前
#include <cmath>
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <optional>
#include <map>
#include <Eigen/Dense>

// 前向声明
class Species;

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
    std::map<std::string, std::vector<std::shared_ptr<Species>>> species_lists;
    int time_step;
    int current_day;
    int current_quadrum;
    int current_year;
    std::string current_quadrum_name;
    Eigen::MatrixXd grass_positions_array; // 对应Python中的numpy数组
    std::vector<std::shared_ptr<Species>> alive_grass_objects;
};

#endif // UTILS_H