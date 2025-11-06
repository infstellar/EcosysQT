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
#include "races_registry.h"
#include "species_statistics.h"
#include "spatial_grid.h"
#include "tile.h"
#include "utils.h"
#include "interaction_requests.h"

// 前向声明避免循环依赖
class ThreadPool;
class ThingBase;
class RaceBase;


// 物种类型枚举已在 species.h 声明

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

// 生态系统配置 (模拟参数)
struct EcosystemConfig {
    int world_width;
    int world_height;
    std::map<std::string, int> initial_populations;
    EcosystemConfig(int w = 800, int h = 600)
        : world_width(w), world_height(h), initial_populations() {}
};

// 生态系统状态管理器 (模拟核心)
class EcosystemState {
public:
    EcosystemConfig config;
    int time_step;
    // 本次更新推进的tick数量（可为小数，用于平滑）
    double delta_ticks;
    RacesRegistry races_registry;
    SpeciesStatistics births;
    SpeciesStatistics deaths;
    std::vector<std::map<std::string, int>> population_history;

    EcosystemState(const EcosystemConfig& config);

    // 用于实时计算时间的 getter 函数
    int get_current_day() const;
    int get_current_quadrum() const;
    int get_current_year() const;
    std::string get_current_quadrum_name() const;

    void initialize_populations();
    EcosystemStateData get_ecosystem_state() const;
    // 基于tick的时间推进（每次更新推进的tick数量）
    void update_time_ticks(double delta_ticks_param);
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
    void update_things();

    std::size_t get_grid_index(int x, int y) const;
    Tile& get_tile(int x, int y);
    const Tile& get_tile(int x, int y) const;
    bool is_valid_grid_coord(int x, int y) const;

    // 访问当前更新推进的tick数量
    double get_delta_ticks() const { return delta_ticks; }
    
    std::vector<std::shared_ptr<RaceBase>> get_nearby_races_broad(
        const Position& center,
        double radius) const;

    std::vector<std::shared_ptr<ThingBase>> get_nearby_things_broad(
        const Position& center,
        double radius) const;

    std::vector<std::shared_ptr<RaceBase>> get_races_in_range(
        const std::string& species_name,
        const Position& center,
        double radius) const;

    std::vector<std::shared_ptr<ThingBase>> get_things_in_range(
        const std::string& species_name,
        const Position& center,
        double radius) const;

    // 并发只读接口：访问空间网格与参数
    const std::vector<std::vector<std::vector<std::shared_ptr<RaceBase>>>>& get_spatial_grid() const { return spatial_grid->cells(); }
    double get_cell_size() const { return spatial_grid->get_cell_size(); }
    int get_grid_width() const { return spatial_grid->get_width(); }
    int get_grid_height() const { return spatial_grid->get_height(); }
    
private:
    // --- 并发阶段共享状态 ---
    // 这些数据结构用于在并发更新的不同阶段之间传递状态。

    // 每个工作线程的交互请求队列，用于无锁地收集来自不同线程的请求。
    std::vector<std::vector<InteractionRequest>> worker_request_queues;
    // 主线程的请求队列（未使用，但可用于调试或单线程回退）。
    std::vector<InteractionRequest> main_thread_requests;
    // 在交互解决阶段，所有工作线程的请求被合并到这里进行处理。
    std::vector<InteractionRequest> staged_requests;

    // RaceBase 状态
    std::unordered_map<RaceBase*, double> race_energy_changes;
    std::unordered_set<RaceBase*> race_marked_for_death;

    // ThingBase 状态
    std::unordered_map<ThingBase*, double> thing_energy_changes;
    std::unordered_set<ThingBase*> thing_marked_for_death;
    // 标记待出生的新物种的父代指针。
    std::vector<std::shared_ptr<RaceBase>> reproduction_parents;
    std::vector<std::shared_ptr<ThingBase>> thing_reproduction_parents;

    std::vector<Tile> m_world_grid;
    std::vector<std::shared_ptr<ThingBase>> m_all_things;

    // 线程局部的随机数生成器。
    static thread_local std::mt19937 thread_local_rng;
    // 线程局部的活动请求队列指针，指向当前线程应该使用的请求队列。
    static thread_local std::vector<InteractionRequest>* tls_active_queue;

    // 激活并返回一个新的请求队列，同时保存前一个队列。
    std::vector<InteractionRequest>* activate_request_queue(std::vector<InteractionRequest>* queue);
    // 恢复到前一个请求队列。
    void restore_request_queue(std::vector<InteractionRequest>* previous_queue);

    void attach_thing_to_world(const std::shared_ptr<ThingBase>& thing);
    void detach_thing_from_tile(ThingBase& thing);

    // --- 空间网格封装 ---
    std::unique_ptr<SpatialGrid> spatial_grid;
};
#endif // ECOSYSTEM_H