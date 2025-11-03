/*
生态系统数据模型
管理整个生态系统状态和数据 (C++ 迁移版本)
*/
#ifndef ECOSYSTEM_H
#define ECOSYSTEM_H
// #pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <optional>
#include "species.h"
#include "species_factory.h"
#include "utils.h"


// 物种类型枚举已在 species.h 声明

// 物种类型与字符串之间的映射函数
SpeciesType species_type_from_name(const std::string& name);
std::string name_from_species_type(SpeciesType type);

// 位置数据，用于序列化/统计 (前端使用)
struct PositionData {
    double x;
    double y;
};

// 个体数据，用于序列化/统计 (前端使用)
struct BaseIndividualData {
    int id;
    PositionData position;
    double energy;
    int age;
    bool alive;
    std::optional<double> max_energy;
};

// 种群数据，用于前端/统计
struct SpeciesPopulationData {
    std::map<std::string, std::vector<BaseIndividualData>> species_data;
};

// 各物种统计信息 (用于种群跟踪)
class SpeciesStatistics {
public:
    std::map<SpeciesType, int> statistics;

    SpeciesStatistics();
    void increment(SpeciesType type, int count = 1);
    void set_count(SpeciesType type, int count);
    int get_count(SpeciesType type) const;
    void reset();

    // 类似属性的访问器，用于兼容性
    int grass() const;
    void set_grass(int value);
    int cow() const;
    void set_cow(int value);
    int tiger() const;
    void set_tiger(int value);
};



// 所有物种类型和个体的注册表
class SpeciesRegistry {
public:
    struct SpeciesInfo {
        std::string name;
        std::vector<std::shared_ptr<Species>> list;
        int initial_count;
    };

    std::map<std::string, SpeciesInfo> registry;

    SpeciesRegistry(const struct EcosystemConfig& config);
    void register_species(const std::string& name, std::shared_ptr<Species> prototype, int initial_count);
    std::vector<std::shared_ptr<Species>>& get_species_list(const std::string& name);
    const std::vector<std::shared_ptr<Species>>& get_species_list(const std::string& name) const;
    int get_initial_count(const std::string& name) const;
    std::vector<std::string> get_all_species_names() const;
    void add_individual(const std::string& name, std::shared_ptr<Species> individual);
    void extend_individuals(const std::string& name, const std::vector<std::shared_ptr<Species>>& individuals);
    void clear_species(const std::string& name);
    void clear_all();
    int get_species_count(const std::string& name) const;
    int get_total_count() const;
    void filter_alive(const std::string& name);
    void filter_all_alive();
};

// 生态系统配置 (模拟参数)
struct EcosystemConfig {
    int world_width;
    int world_height;
    int initial_grass;
    int initial_cows;
    int initial_tigers;
    EcosystemConfig(int w = 800, int h = 600, int g = 100, int c = 10, int t = 1)
        : world_width(w), world_height(h), initial_grass(g), initial_cows(c), initial_tigers(t) {}
};

// 生态系统状态管理器 (模拟核心)
class EcosystemState {
public:
    EcosystemConfig config;
    int time_step;
    SpeciesRegistry species_registry;
    SpeciesStatistics births;
    SpeciesStatistics deaths;
    std::vector<std::map<SpeciesType, int>> population_history;

    EcosystemState(const EcosystemConfig& config);

    void initialize_populations();
    EcosystemStateData get_ecosystem_state() const;
    void update_species();
    void handle_reproduction();
    void update_statistics();
    void cleanup_dead();
    SpeciesStatistics get_species_counts() const;
    SpeciesPopulationData get_species_data() const;
    void reset(const EcosystemConfig& config);
    std::vector<std::string> check_extinction() const;
    
    // 通用查询接口：获取指定范围内的物种个体
    std::vector<std::shared_ptr<Species>> get_species_in_range(
        const std::string& species_name, 
        const Position& center, 
        double radius) const;
};
#endif // ECOSYSTEM_H