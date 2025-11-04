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
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include "species.h"
#include "species_factory.h"
#include "utils.h"
#include "interaction_requests.h"

// 前向声明避免循环依赖
class ThreadPool;


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

    // 用于实时计算时间的 getter 函数
    int get_current_day() const;
    int get_current_quadrum() const;
    int get_current_year() const;
    std::string get_current_quadrum_name() const;

    void initialize_populations();
    EcosystemStateData get_ecosystem_state() const;
    void update_time();
    void update_statistics();

    // --- 新的并发更新阶段 ---
    // 这些方法构成了并发更新循环的核心，取代了原有的单线程 `update_species`。

    // 准备阶段：在并发更新前调用，用于构建空间哈希等准备工作。
    void prepare_for_update();
    // 决策任务分派：将所有物种的决策任务（如移动、觅食）提交到线程池。
    void dispatch_decision_tasks(ThreadPool& pool);
    // 交互解决：在所有决策任务完成后，同步处理它们之间的交互（如捕食）。
    void resolve_interactions();
    // 应用任务分派：将所有物种的状态更新任务（如能量变化、位置更新）提交到线程池。
    void dispatch_apply_tasks(ThreadPool& pool);
    // 应用注册表变更：在所有更新应用后，统一处理物种的出生和死亡。
    void apply_registry_changes();

    // --- 线程安全 RNG ---
    // 为每个线程提供一个独立的随机数生成器，避免锁竞争。
    std::mt19937& get_thread_local_rng();

    // 决策阶段提交交互请求（线程本地，无锁）
    // 在决策阶段，物种可以通过此方法提交交互请求（如捕食），这些请求将被暂存并在稍后解决。
    void submit_interaction_request(InteractionRequest request);
    SpeciesStatistics get_species_counts() const;
    SpeciesPopulationData get_species_data() const;
    void reset(const EcosystemConfig& config);
    std::vector<std::string> check_extinction() const;
    
    // 通用查询接口：获取指定范围内的物种个体
    std::vector<std::shared_ptr<Species>> get_species_in_range(
        const std::string& species_name, 
        const Position& center, 
        double radius) const;

    // 并发只读接口：访问空间网格与参数
    const std::vector<std::vector<std::vector<std::shared_ptr<Species>>>>& get_spatial_grid() const { return spatial_grid; }
    double get_cell_size() const { return cell_size; }
    int get_grid_width() const { return grid_width; }
    int get_grid_height() const { return grid_height; }
    
private:
    // --- 并发阶段共享状态 ---
    // 这些数据结构用于在并发更新的不同阶段之间传递状态。

    // 每个工作线程的交互请求队列，用于无锁地收集来自不同线程的请求。
    std::vector<std::vector<InteractionRequest>> worker_request_queues;
    // 主线程的请求队列（未使用，但可用于调试或单线程回退）。
    std::vector<InteractionRequest> main_thread_requests;
    // 在交互解决阶段，所有工作线程的请求被合并到这里进行处理。
    std::vector<InteractionRequest> staged_requests;

    // 存储能量变化的映射，键为物种指针，值为能量变化量。
    std::unordered_map<Species*, double> energy_changes;
    // 标记待移除的物种集合。
    std::unordered_set<Species*> marked_for_death;
    // 标记待出生的新物种的位置列表。
    std::vector<Position> marked_for_birth;

    // 线程局部的随机数生成器。
    static thread_local std::mt19937 thread_local_rng;
    // 线程局部的活动请求队列指针，指向当前线程应该使用的请求队列。
    static thread_local std::vector<InteractionRequest>* tls_active_queue;

    // 激活并返回一个新的请求队列，同时保存前一个队列。
    std::vector<InteractionRequest>* activate_request_queue(std::vector<InteractionRequest>* queue);
    // 恢复到前一个请求队列。
    void restore_request_queue(std::vector<InteractionRequest>* previous_queue);

    // --- 均匀网格 (Spatial Hash) ---
    // 网格本身：一个2D数组，每个单元格(Cell)包含一个物种指针列表
    std::vector<std::vector<std::vector<std::shared_ptr<Species>>>> spatial_grid;
    // 网格参数
    double cell_size{100.0};
    int grid_width{0};
    int grid_height{0};
};
#endif // ECOSYSTEM_H