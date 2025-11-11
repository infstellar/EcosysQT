/*
生态系统数据模型
管理整个生态系统状态和数据 (C++ 迁移版本)
*/

#include "ecosystem.h"
#include "animal.h"
#include "race_factory.h"
#include "thing_factory.h"
#include "thing_base.h"
#include "thread_pool.h"
#include "tracy/Tracy.hpp"
#include <random>
#include <algorithm>
#include <Eigen/Dense>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <queue>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <utility>

thread_local std::mt19937 EcosystemState::thread_local_rng{std::random_device{}()};
thread_local std::vector<InteractionRequest>* EcosystemState::tls_active_queue = nullptr;

namespace {

double squared_distance(const Position& a, const Position& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

// 曼哈顿距离（L1范数）：两点间距离
double manhattan_distance(const Position& a, const Position& b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

double min_distance_sq_to_rect(const Position& point,
                               double min_x,
                               double min_y,
                               double max_x,
                               double max_y) {
    double dx = 0.0;
    if (point.x < min_x) {
        dx = min_x - point.x;
    } else if (point.x > max_x) {
        dx = point.x - max_x;
    }

    double dy = 0.0;
    if (point.y < min_y) {
        dy = min_y - point.y;
    } else if (point.y > max_y) {
        dy = point.y - max_y;
    }

    return (dx * dx) + (dy * dy);
}

// 点到矩形的最短曼哈顿距离（L1）：若在范围内则相应维度距离为0
double min_manhattan_distance_to_rect(const Position& point,
                                      double min_x,
                                      double min_y,
                                      double max_x,
                                      double max_y) {
    double dx = 0.0;
    if (point.x < min_x) {
        dx = min_x - point.x;
    } else if (point.x > max_x) {
        dx = point.x - max_x;
    }

    double dy = 0.0;
    if (point.y < min_y) {
        dy = min_y - point.y;
    } else if (point.y > max_y) {
        dy = point.y - max_y;
    }

    return dx + dy;
}

double min_distance_sq_to_tile(const Position& point, int tile_x, int tile_y) {
    const double min_x = static_cast<double>(tile_x);
    const double min_y = static_cast<double>(tile_y);
    return min_distance_sq_to_rect(point, min_x, min_y, min_x + 1.0, min_y + 1.0);
}

double min_distance_sq_to_spatial_cell(const Position& point,
                                       int cell_x,
                                       int cell_y,
                                       double cell_size) {
    const double min_x = static_cast<double>(cell_x) * cell_size;
    const double min_y = static_cast<double>(cell_y) * cell_size;
    return min_distance_sq_to_rect(point, min_x, min_y, min_x + cell_size, min_y + cell_size);
}

// 点到空间网格单元（cell）的最短曼哈顿距离（L1）
double min_manhattan_distance_to_spatial_cell(const Position& point,
                                              int cell_x,
                                              int cell_y,
                                              double cell_size) {
    const double min_x = static_cast<double>(cell_x) * cell_size;
    const double min_y = static_cast<double>(cell_y) * cell_size;
    return min_manhattan_distance_to_rect(point, min_x, min_y, min_x + cell_size, min_y + cell_size);
}

template <typename T>
struct NearestCandidate {
    double distance_sq;
    std::shared_ptr<T> target;
};

template <typename T>
struct FarthestFirst {
    bool operator()(const NearestCandidate<T>& lhs, const NearestCandidate<T>& rhs) const {
        return lhs.distance_sq < rhs.distance_sq;
    }
};

template <typename T>
struct NearestCandidateRaw {
    double distance_sq;
    T* target;
};

template <typename T>
struct FarthestFirstRaw {
    bool operator()(const NearestCandidateRaw<T>& lhs, const NearestCandidateRaw<T>& rhs) const {
        return lhs.distance_sq < rhs.distance_sq;
    }
};

} // namespace

// --- EcosystemState ---
// 生态系统状态管理器 (模拟核心)
EcosystemState::EcosystemState(const EcosystemConfig& config)
            : config(config),
                races_registry(config),
                births(),
                deaths(),
                population_history(),
                spatial_grid(std::make_unique<SpatialGrid>(config.world_width, config.world_height, 100.0)),
                m_world_grid(config.world_width, config.world_height),
                m_all_things() {
    m_clock.attach_config(&this->config);
    initialize_populations();
}

/*
使用统一逻辑初始化所有物种的种群
*/
void EcosystemState::initialize_populations() {
    auto logger = spdlog::get("ecosim");
    m_world_grid.resize(config.world_width, config.world_height);
    m_world_grid.clear_things();
    m_all_things.clear();
    m_thing_counts.clear();
    // 动物初始化块
    auto init_animals = [&]() {
        auto race_names = races_registry.get_all_species_names();
        if (logger) {
            logger->info("[Init] Initializing populations for {} races", race_names.size());
        }
        for (const auto& name : race_names) {
            int initial_count = races_registry.get_initial_count(name);
            if (logger) {
                logger->info("[Init] '{}' initial count: {}", name, initial_count);
            }
            for (int i = 0; i < initial_count; ++i) {
                std::uniform_real_distribution<> distX(0, config.world_width);
                std::uniform_real_distribution<> distY(0, config.world_height);
                int x = distX(get_thread_local_rng());
                int y = distY(get_thread_local_rng());
                try {
                    auto new_individual = g_race_factory.create(name, Position{static_cast<double>(x), static_cast<double>(y)}, get_thread_local_rng());
                    races_registry.add_individual(name, std::move(new_individual));
                } catch (const std::exception& e) {
                    if (logger) {
                        logger->error("[Init] Failed to create instance for '{}' at index {}: {}", name, i, e.what());
                    }
                    throw;
                }
            }
        }
    };

    // 事物初始化块
    auto init_things = [&]() {
        auto thing_names = g_thing_factory.get_all_species_names();
        if (logger) {
            logger->info("[Init] Initializing things for {} species", thing_names.size());
        }

        std::mt19937& rng = get_thread_local_rng();
        std::uniform_int_distribution<int> dist_tile_x(0, config.world_width - 1);
        std::uniform_int_distribution<int> dist_tile_y(0, config.world_height - 1);

        for (const auto& name : thing_names) {
            int initial_count = 0;
            auto it = config.initial_populations.find(name);
            if (it != config.initial_populations.end()) {
                initial_count = it->second;
            }
            if (logger) {
                logger->info("[Init] '{}' initial thing count: {}", name, initial_count);
            }

            int attempts = 0;
            for (int i = 0; i < initial_count; ++i) {
                const int kMaxPlacementAttempts = config.max_thing_placement_attempts;
                bool placed = false;
                while (!placed && attempts < kMaxPlacementAttempts * initial_count) {
                    ++attempts;
                    int tile_x = dist_tile_x(rng);
                    int tile_y = dist_tile_y(rng);
                    Tile& tile = m_world_grid.get_tile(tile_x, tile_y);
                    if (tile.terrain != TerrainType::LAND || !tile.things.empty()) {
                        continue;
                    }

                    Position world_pos{static_cast<double>(tile_x) + 0.5, static_cast<double>(tile_y) + 0.5};
                    auto thing_unique = g_thing_factory.create(name, world_pos, rng);
                    if (!thing_unique) {
                        if (logger) {
                            logger->warn("[Init] Thing factory returned null for '{}'", name);
                        }
                        break;
                    }

                    std::shared_ptr<ThingBase> thing(std::move(thing_unique));
                    thing->position = world_pos;
                    thing->m_grid_x = tile_x;
                    thing->m_grid_y = tile_y;
                    attach_thing_to_world(thing);
                    placed = true;
                }

                if (!placed && logger) {
                    logger->warn("[Init] Unable to place initial '{}' after {} attempts", name, attempts);
                }
            }
        }
    };

    // 执行
    init_animals();
    if (config.world_width <= 0 || config.world_height <= 0) {
        if (logger) {
            logger->warn("[Init] World dimensions are non-positive; skipping thing initialization");
        }
        return;
    }
    init_things();
}

void EcosystemState::attach_thing_to_world(const std::shared_ptr<ThingBase>& thing) {
    if (!thing) {
        return;
    }
    m_world_grid.add_thing_to_tile(thing.get());
    m_all_things.push_back(thing);
    if (thing->alive) {
        ++m_thing_counts[thing->species_name];
    }
}

void EcosystemState::detach_thing_from_tile(ThingBase& thing) {
    m_world_grid.remove_thing_from_tile(thing);
    thing.m_grid_x = -1;
    thing.m_grid_y = -1;
}

/*
获取用于模拟和前端的生态系统状态快照
*/
EcosystemStateData EcosystemState::get_ecosystem_state() const {
    EcosystemStateData state;
    state.world_width = config.world_width;
    state.world_height = config.world_height;
    state.time_step = m_clock.time_step();
    state.current_day = m_clock.current_day();
    state.current_quadrum = m_clock.current_quadrum();
    state.current_year = m_clock.current_year();
    state.current_quadrum_name = m_clock.current_quadrum_name();
    state.current_hour = m_clock.current_hour();
    state.current_minute = m_clock.current_minute();
    // 填充 race_lists
    for (const auto& species_name : races_registry.get_all_species_names()) {
        const auto& race_list = races_registry.get_species_list(species_name);
        std::vector<std::shared_ptr<RaceBase>> snapshot;
        snapshot.reserve(race_list.size());
        for (const auto& race : race_list) {
            if (race) {
                snapshot.push_back(race);
            }
        }
        state.race_lists[species_name] = std::move(snapshot);
    }

    // --- 优化：使用 m_thing_counts 进行预分配，并仅收集存活对象 ---
    for (const auto& pair : m_thing_counts) {
        const std::string& species_name = pair.first;
        const std::size_t count = pair.second;
        if (count > 0) {
            state.thing_lists[species_name].reserve(count);
        }
    }
    for (const auto& thing : m_all_things) {
        if (thing && thing->alive) {
            state.thing_lists[thing->species_name].push_back(thing);
        }
    }

    // 预计算草的位置和存活对象 (Eigen矩阵)
    // std::vector<Eigen::Vector2d> alive_grass_positions;

    // auto grass_it = state.thing_lists.find("grass");
    // if (grass_it != state.thing_lists.end()) {
    //     for (const auto& grass : grass_it->second) {
    //         if (grass && grass->alive) {
    //             state.alive_grass_objects.push_back(grass);
    //             alive_grass_positions.emplace_back(grass->position.x, grass->position.y);
    //         }
    //     }
    // }

    // if (!alive_grass_positions.empty()) {
    //     state.grass_positions_array = Eigen::MatrixXd(alive_grass_positions.size(), 2);
    //     for (size_t i = 0; i < alive_grass_positions.size(); ++i) {
    //         state.grass_positions_array(i, 0) = alive_grass_positions[i](0);
    //         state.grass_positions_array(i, 1) = alive_grass_positions[i](1);
    //     }
    // } else {
    //     state.grass_positions_array = Eigen::MatrixXd(0, 2);
    // }
    return state;
}

/*
使用统一逻辑更新时间状态
*/
void EcosystemState::update_one_tick() {
    // 离散锁步：每次调用推进一个整数tick
    m_clock.advance_tick();
}

/*
更新统计信息并维护种群历史
*/
void EcosystemState::update_statistics() {
    SpeciesStatistics stats = get_species_counts();
        std::map<std::string, int> snapshot = stats.statistics;
    population_history.push_back(snapshot);
    if (population_history.size() > 100)
        population_history.erase(population_history.begin(), population_history.end() - 100);
}

/**
 * @brief 为并发更新周期准备生态系统状态。
 *
 * 这是多阶段并发更新的第一步。此函数通过清理上一周期的状态并重建
 * 空间哈希网格来为新的模拟周期做准备。主要操作包括：
 * 1. 清空空间网格，为重新填充做准备。
 * 2. 清理所有用于并发控制的请求队列和状态跟踪器（如死亡、出生、能量变化等）。
 * 3. 遍历所有存活的个体，根据它们当前的位置将其重新插入到空间网格中。
 *    这确保了在决策阶段，所有空间查询（如邻居查找）都使用最新的数据。
 */
void EcosystemState::prepare_for_update() {
    // 标记当前阶段为准备阶段
    current_phase = UpdatePhase::Prepare;
    staged_requests.clear();      // 清空暂存的交互请求
    main_thread_requests.clear(); // 清空主线程处理的请求
    m_resolution_state.clear();   // 重置上一周期的交互解决结果

    if (!spatial_grid) {
        return;
    }

    spatial_grid->build(races_registry);
}

/**
 * @brief 将决策阶段的任务分派给线程池。
 *
 * 这个函数负责将生态系统中所有物种的决策过程并行化。它将每个物种的个体列表（动物和植物）分成块，
 * 并为每个块提交一个任务到线程池中。
 *
 * 主要步骤如下：
 * 1.  **调整工作队列**：确保每个工作线程都有一个请求队列，用于存储交互请求。
 * 2.  **定义块提交逻辑**：创建一个 lambda 函数 `submit_chunk`，用于将指定范围的个体提交给线程池执行决策。
 * 3.  **任务并行执行**：
 *      - 在每个任务中，首先确定当前线程的工作索引。
 *      - 为当前线程激活对应的请求队列，以便在决策过程中可以安全地提交交互请求。
 *      - 遍历块中的每个个体，调用其 `decide` 方法。
 *      - 决策完成后，恢复之前的请求队列状态。
 * 4.  **分派任务**：遍历所有动物和植物，使用 `submit_chunk` 将它们分块并提交到线程池。
 *
 * @param pool 用于执行任务的线程池。
 */
void EcosystemState::dispatch_decision_tasks(ThreadPool& pool) {
    current_phase = UpdatePhase::Decision;

    constexpr std::size_t heavy_chunk_size = 2;
    constexpr std::size_t light_chunk_size = 2048;

    const std::size_t worker_count = std::max<std::size_t>(1, pool.worker_count());
    if (worker_request_queues.size() != worker_count) {
        worker_request_queues.assign(worker_count, {});
    }
    for (auto& queue : worker_request_queues) {
        queue.clear();
    }

    std::vector<std::shared_ptr<RaceBase>> all_races_to_update;
    const auto species_names = races_registry.get_all_species_names();
    std::size_t total_races = 0;
    for (const auto& name : species_names) {
        total_races += races_registry.get_species_list(name).size();
    }
    all_races_to_update.reserve(total_races);
    for (const auto& name : species_names) {
        auto& list = races_registry.get_species_list(name);
        all_races_to_update.insert(all_races_to_update.end(), list.begin(), list.end());
        }
    auto races_agg = std::make_shared<std::vector<std::shared_ptr<RaceBase>>>(std::move(all_races_to_update));

    std::vector<std::shared_ptr<ThingBase>>& all_things_to_update = m_all_things;

    std::vector<std::function<void()>> master_task_list;
    master_task_list.reserve(
        (races_agg->size() / heavy_chunk_size) +
        (all_things_to_update.size() / light_chunk_size) + 2
    );

    if (!races_agg->empty()) {
        for (std::size_t begin = 0; begin < races_agg->size(); begin += heavy_chunk_size) {
            const std::size_t end = std::min(begin + heavy_chunk_size, races_agg->size());
            master_task_list.push_back([this, races_agg, begin, end] {
                const auto worker_index = ThreadPool::current_worker_index();
                std::vector<InteractionRequest>* active_queue = nullptr;
                if (worker_index < worker_request_queues.size()) {
                    active_queue = &worker_request_queues[worker_index];
                }
                auto* previous_queue = activate_request_queue(active_queue);
                auto& rng = get_thread_local_rng();

                for (std::size_t i = begin; i < end; ++i) {
                    auto& individual = (*races_agg)[i];
                    if (individual) {
                        individual->decide(*this, rng);
                    }
                }

                restore_request_queue(previous_queue);
            });
        }
    }

    if (!all_things_to_update.empty()) {
        for (std::size_t begin = 0; begin < all_things_to_update.size(); begin += light_chunk_size) {
            const std::size_t end = std::min(begin + light_chunk_size, all_things_to_update.size());
            master_task_list.push_back([this, &all_things_to_update, begin, end] {
                const auto worker_index = ThreadPool::current_worker_index();
                std::vector<InteractionRequest>* active_queue = nullptr;
                if (worker_index < worker_request_queues.size()) {
                    active_queue = &worker_request_queues[worker_index];
                }
                auto* previous_queue = activate_request_queue(active_queue);
                auto& rng = get_thread_local_rng();

                for (std::size_t i = begin; i < end; ++i) {
                    auto& individual = all_things_to_update[i];
                    if (individual) {
                        individual->decide(*this, rng);
                    }
                }

                restore_request_queue(previous_queue);
            });
        }
    }

    auto& rng = get_thread_local_rng();
    std::shuffle(master_task_list.begin(), master_task_list.end(), rng);

    pool.submit_bulk(std::move(master_task_list));
}

// Consolidate per-thread queues into the shared staging buffer.
void EcosystemState::merge_worker_queues() {
    staged_requests.clear();

    for (auto& queue : worker_request_queues) {
        if (queue.empty()) {
            continue;
        }
        staged_requests.insert(staged_requests.end(),
                               std::make_move_iterator(queue.begin()),
                               std::make_move_iterator(queue.end()));
        queue.clear();
    }

    if (!main_thread_requests.empty()) {
        staged_requests.insert(staged_requests.end(),
                               std::make_move_iterator(main_thread_requests.begin()),
                               std::make_move_iterator(main_thread_requests.end()));
        main_thread_requests.clear();
    }
}

/**
 * @brief 解决在决策阶段产生的所有交互请求。
 *
 * 此函数是并发更新的第二阶段（交互解决阶段）。它首先将所有工作线程的
 * 本地请求队列中的请求移动到一个统一的 `staged_requests` 队列中，并清理上
 * 一轮的解析状态，然后将请求批次交给 `InteractionResolver` 处理，后者负责根
 * 据请求类型更新本轮的死亡标记、能量变化和繁殖登记等结果。
 *
 * 这是一个同步点，确保在进入下一阶段（应用阶段）之前，所有交互都已解决。
 */
void EcosystemState::resolve_interactions() {
    // 标记当前阶段为交互解决阶段
    current_phase = UpdatePhase::Resolve;
    merge_worker_queues();
    m_resolution_state.clear();

    // 如果没有需要处理的请求，则提前返回。
    if (staged_requests.empty()) {
        return;
    }

    m_interaction_resolver.process_requests(staged_requests, *this, m_resolution_state);
}

/**
 * @brief 将所有物种的状态应用任务分派到线程池。
 *
 * （此阶段目前为占位符，预留用于未来的并发应用逻辑）。
 * 在这个阶段，可以并行地应用在 `resolve_interactions` 中计算出的状态变更，
 * 例如，实际更新物种的能量、位置等。
 *
 * @param pool 要使用的线程池。
 */
void EcosystemState::dispatch_apply_tasks(ThreadPool& pool) {
    current_phase = UpdatePhase::Apply;

    constexpr std::size_t heavy_chunk_size = 64;
    constexpr std::size_t light_chunk_size = 4096;

    std::vector<std::shared_ptr<RaceBase>> all_races_to_update;
    const auto species_names = races_registry.get_all_species_names();
    std::size_t total_races = 0;
    for (const auto& name : species_names) {
        total_races += races_registry.get_species_list(name).size();
    }
    all_races_to_update.reserve(total_races);
    for (const auto& name : species_names) {
        auto& list = races_registry.get_species_list(name);
        all_races_to_update.insert(all_races_to_update.end(), list.begin(), list.end());
    }
    auto races_agg = std::make_shared<std::vector<std::shared_ptr<RaceBase>>>(std::move(all_races_to_update));

    std::vector<std::shared_ptr<ThingBase>>& all_things_to_update = m_all_things;

    std::vector<std::function<void()>> master_task_list;
    master_task_list.reserve(
        (races_agg->size() / heavy_chunk_size) +
        (all_things_to_update.size() / light_chunk_size) + 2
    );

    if (!races_agg->empty()) {
        for (std::size_t begin = 0; begin < races_agg->size(); begin += heavy_chunk_size) {
            const std::size_t end = std::min(begin + heavy_chunk_size, races_agg->size());
            master_task_list.push_back([this, races_agg, begin, end] {
                for (std::size_t i = begin; i < end; ++i) {
                    auto& individual = (*races_agg)[i];
                    if (individual) {
                        individual->apply(*this);
                    }
                }
            });
        }
    }

    if (!all_things_to_update.empty()) {
        for (std::size_t begin = 0; begin < all_things_to_update.size(); begin += light_chunk_size) {
            const std::size_t end = std::min(begin + light_chunk_size, all_things_to_update.size());
            master_task_list.push_back([this, &all_things_to_update, begin, end] {
                for (std::size_t i = begin; i < end; ++i) {
                    auto& individual = all_things_to_update[i];
                    if (individual) {
                        individual->apply(*this);
                    }
                }
            });
        }
    }

    if (!master_task_list.empty()) {
        std::shuffle(master_task_list.begin(), master_task_list.end(), get_thread_local_rng());
    }

    pool.submit_bulk(std::move(master_task_list));
}

/**
 * @brief 应用所有在更新周期中累积的注册表变更。
 *
 * 此函数是更新周期的最后阶段，负责处理物种的出生和死亡。
 * 它会遍历所有物种，处理繁殖请求（创建新个体），并根据 `race_marked_for_death`
 * 集合移除死亡的个体。同时，它也会应用累积的能量变化，并更新统计数据。
 *
 * 将这些变更放在最后统一处理，可以避免在迭代物种列表时修改它，从而简化
 * 并发控制和逻辑。
 */
void EcosystemState::apply_registry_changes() {
    // 标记当前阶段为最终化阶段
    current_phase = UpdatePhase::Finalize;
    m_population_manager.apply_changes(*this);
    m_resolution_state.clear();
    staged_requests.clear();
    current_phase = UpdatePhase::Idle;
}

/**
 * @brief 获取当前线程的本地随机数生成器。
 *
 * @return std::mt19937& 对线程局部RNG的引用。
 */
std::mt19937& EcosystemState::get_thread_local_rng() {
    return thread_local_rng;
}

/**
 * @brief 提交一个交互请求到当前线程的活动队列。
 *
 * @param request 要提交的交互请求。
 */
void EcosystemState::submit_interaction_request(InteractionRequest request) {
    // 仅允许在决策阶段提交交互请求，其他阶段拒绝并记录日志
    if (current_phase != UpdatePhase::Decision) {
        auto logger = spdlog::get("ecosim");
        if (logger) {
            logger->warn("Rejecting interaction request outside Decision phase (phase={})", static_cast<int>(current_phase));
        }
        return;
    }
    // 如果当前线程有一个活动的请求队列（在工作线程中），则将请求添加到该队列。
    if (tls_active_queue) {
        tls_active_queue->push_back(std::move(request));
    } else {
        // 否则（在主线程或单线程模式下），将请求添加到主线程的请求队列。
        main_thread_requests.push_back(std::move(request));
    }
}

/**
 * @brief 激活一个新的请求队列作为当前线程的活动队列。
 *
 * @param queue 要激活的队列的指针。
 * @return std::vector<InteractionRequest>* 先前活动的队列的指针，用于稍后恢复。
 */
std::vector<InteractionRequest>* EcosystemState::activate_request_queue(std::vector<InteractionRequest>* queue) {
    auto* previous = tls_active_queue;
    tls_active_queue = queue;
    return previous;
}

/**
 * @brief 恢复先前活动的请求队列。
 *
 * @param previous_queue 要恢复的队列的指针。
 */
void EcosystemState::restore_request_queue(std::vector<InteractionRequest>* previous_queue) {
    tls_active_queue = previous_queue;
}

/**
 * @brief 获取所有物种的当前种群数量。
 *
 * 此函数遍历所有已注册的物种，并查询它们当前的个体数量，
 * 然后将结果汇总到一个 `SpeciesStatistics` 对象中。
 *
 * @return 包含各种群数量的 `SpeciesStatistics` 对象。
 */
SpeciesStatistics EcosystemState::get_species_counts() const {
    SpeciesStatistics stats;
    for (const auto& name : races_registry.get_all_species_names()) {
        int count = races_registry.get_species_count(name);
        stats.set_count(name, count);
    }
    return stats;
}

/**
 * @brief 获取所有物种的详细个体数据，主要用于前端显示或详细分析。
 *
 * 此函数遍历所有物种，并为每个存活的个体收集详细信息，
 * 如 ID、位置、能量、年龄等，然后将这些数据组织成 `SpeciesPopulationData` 结构。
 *
 * @return 包含所有物种详细个体数据的 `SpeciesPopulationData` 对象。
 */
SpeciesPopulationData EcosystemState::get_species_data() const {
    SpeciesPopulationData data;
    for (const auto& name : races_registry.get_all_species_names()) {
        const auto& list = races_registry.get_species_list(name);
        std::vector<BaseIndividualData> individuals;
        for (const auto& individual : list) {
            if (individual->alive) {
                BaseIndividualData ind;
                // 使用个体的内存地址作为其唯一ID。这在C++端是可行的，但在跨语言
                // 或持久化场景下需要更稳定的ID生成策略。
                ind.id = reinterpret_cast<std::uintptr_t>(individual.get());
                ind.position = PositionData{individual->position.x, individual->position.y};
                ind.energy = individual->energy;
                ind.age = individual->age;
                ind.alive = individual->alive;
                ind.max_energy = individual->max_energy;
                individuals.push_back(ind);
            }
        }
        data.species_data[name] = individuals;
    }
    return data;
}

/**
 * @brief 将生态系统重置为初始状态。
 *
 * 此函数用于重置整个模拟，包括时间、所有种群、统计数据和历史记录。
 * 它会应用新的配置，并重新初始化种群。
 *
 * @param new_config 要应用的新生态系统配置。
 */
void EcosystemState::reset(const EcosystemConfig& new_config) {
    config = new_config;
    m_clock.attach_config(&config);
    m_clock.reset();
    // 重新构造注册表以应用新的初始数量
    races_registry = RacesRegistry(config);
    births.reset();
    deaths.reset();
    population_history.clear();
    // 清理并发更新相关的状态
    staged_requests.clear();
    main_thread_requests.clear();
    m_resolution_state.clear();
    m_thing_counts.clear();
    const double cell = spatial_grid ? spatial_grid->get_cell_size() : 100.0;
    spatial_grid = std::make_unique<SpatialGrid>(config.world_width, config.world_height, cell);
    initialize_populations();
}

/**
 * @brief 检查并返回已灭绝的物种列表。
 *
 * 如果一个物种的种群数量降为 0，则认为该物种已灭绝。
 *
 * @return 包含所有已灭绝物种名称的字符串向量。
 */
std::vector<std::string> EcosystemState::check_extinction() const {
    std::vector<std::string> extinct;
    for (const auto& name : races_registry.get_all_species_names()) {
        if (races_registry.get_species_count(name) == 0)
            extinct.push_back(name);
    }
    return extinct;
}

/**
 * @brief 在指定的圆形范围内查询特定物种的个体。
 *
 * 这是一个通用的空间查询接口，用于查找给定中心点和半径范围内的所有
 * 存活个体。这是一个简单的线性扫描实现，对于大规模查询，可以替换为
 * 基于空间哈希网格的更高效实现。
 *
 * @param species_name 要查询的物种名称。
 * @param center 查询区域的中心点。
 * @param radius 查询区域的半径。
 * @return 在指定范围内的所有存活个体的共享指针列表。
 */
std::vector<std::shared_ptr<RaceBase>> EcosystemState::get_nearby_races_broad(
    const Position& center,
    double radius) const {

    ZoneScoped;
    if (!spatial_grid) {
        return {};
    }

    return spatial_grid->get_nearby_races_broad(center, radius);
}

std::vector<std::shared_ptr<ThingBase>> EcosystemState::get_nearby_things_broad(
    const Position& center,
    double radius) const {
    return m_world_grid.get_nearby_things_broad(center, radius);
}

std::vector<std::shared_ptr<RaceBase>> EcosystemState::get_races_in_range(
    const std::vector<std::string>& species_names,
    const Position& center,
    double radius) const {
    if (radius < 0.0 || species_names.empty()) {
        return {};
    }
    ZoneScoped;
    const auto nearby_races = get_nearby_races_broad(center, radius);
    std::vector<std::shared_ptr<RaceBase>> results;
    results.reserve(nearby_races.size());
    const double radius_sq = radius * radius;

    for (const auto& race : nearby_races) {
        if (!race || !race->alive) {
            continue;
        }
        if (std::find(species_names.begin(), species_names.end(), race->species_name) == species_names.end()) {
            continue;
        }

        const double dx = race->position.x - center.x;
        const double dy = race->position.y - center.y;
        if ((dx * dx + dy * dy) <= radius_sq) {
            results.push_back(race);
        }
    }

    return results;
}

std::vector<std::shared_ptr<ThingBase>> EcosystemState::get_things_in_range(
    const std::vector<std::string>& species_names,
    const Position& center,
    double radius) const {
    if (radius < 0.0 || species_names.empty()) {
        return {};
    }
    ZoneScoped;
    const auto nearby_things = get_nearby_things_broad(center, radius);
    std::vector<std::shared_ptr<ThingBase>> results;
    results.reserve(nearby_things.size());
    const double radius_sq = radius * radius;

    for (const auto& thing : nearby_things) {
        if (!thing || !thing->alive) {
            continue;
        }
        if (std::find(species_names.begin(), species_names.end(), thing->species_name) == species_names.end()) {
            continue;
        }

        const double dx = thing->position.x - center.x;
        const double dy = thing->position.y - center.y;
        if ((dx * dx + dy * dy) <= radius_sq) {
            results.push_back(thing);
        }
    }

    return results;
}

std::vector<std::shared_ptr<ThingBase>> EcosystemState::find_nearest_things(
    const Position& center,
    const std::vector<std::string>& species_names,
    std::size_t n,
    double max_radius) const {
    ZoneScoped;

    // --- 边界条件 ---
    if (n == 0 || max_radius < 0.0 || species_names.empty()) {
        return {};
    }

    const int grid_width = m_world_grid.width();
    const int grid_height = m_world_grid.height();
    if (grid_width <= 0 || grid_height <= 0) {
        return {};
    }

    const double max_radius_sq = max_radius * max_radius;

    // --- 结果与过滤集初始化 ---
    std::vector<std::shared_ptr<ThingBase>> results;
    results.reserve(n);

    const std::unordered_set<std::string> species_filter(species_names.begin(), species_names.end());
    std::unordered_set<const ThingBase*> added_things;

    // --- BFS 队列与本地访问网格 ---
    std::queue<std::pair<int, int>> frontier;
    const int radius_in_tiles = static_cast<int>(std::ceil(max_radius));
    const int local_grid_dim = 2 * radius_in_tiles + 1;
    const std::size_t local_total_size = static_cast<std::size_t>(local_grid_dim) * static_cast<std::size_t>(local_grid_dim);
    std::vector<char> local_visited(local_total_size, 0); // Local grid keeps visited markers bounded by radius

    int start_x = static_cast<int>(std::floor(center.x));
    int start_y = static_cast<int>(std::floor(center.y));
    start_x = std::clamp(start_x, 0, grid_width - 1);
    start_y = std::clamp(start_y, 0, grid_height - 1);
    const int local_origin_x = start_x - radius_in_tiles;
    const int local_origin_y = start_y - radius_in_tiles;

    // 8方向（菱形螺旋的广义邻域）
    const std::array<std::pair<int, int>, 8> directions{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    }};

    // 入队辅助函数
    const auto try_enqueue = [&](int x, int y) {
        if (x < 0 || x >= grid_width || y < 0 || y >= grid_height) {
            return;
        }

        const int local_x = x - local_origin_x;
        const int local_y = y - local_origin_y;
        if (local_x < 0 || local_x >= local_grid_dim || local_y < 0 || local_y >= local_grid_dim) {
            return;
        }

        const std::size_t local_idx = static_cast<std::size_t>(local_y) * static_cast<std::size_t>(local_grid_dim) + static_cast<std::size_t>(local_x);
        if (local_visited[local_idx]) {
            return;
        }
        local_visited[local_idx] = 1;

        // 半径剪枝：该地块的最近点也超出半径则跳过
        if (min_distance_sq_to_tile(center, x, y) > max_radius_sq) {
            return;
        }

        frontier.emplace(x, y);
    };

    // 起始地块（准备BFS）
    {
        ZoneScopedN("Setup BFS");
        try_enqueue(start_x, start_y);
    }

    // --- 螺旋（BFS）搜索 ---
    int cnt = 0;
    {
        ZoneScopedN("Exec BFS");
        while (!frontier.empty()) {
            cnt++;
            auto [tile_x, tile_y] = frontier.front();
            frontier.pop();

        const Tile& tile = m_world_grid.get_tile(tile_x, tile_y);
        for (ThingBase* thing_ptr : tile.things) {
            if (!thing_ptr || !thing_ptr->alive) {
                continue;
            }
            if (added_things.count(thing_ptr)) {
                continue;
            }
            if (species_filter.find(thing_ptr->species_name) == species_filter.end()) {
                continue;
            }

            if (squared_distance(center, thing_ptr->position) <= max_radius_sq) {
                results.push_back(thing_ptr->shared_from_this());
                added_things.insert(thing_ptr);

                if (results.size() >= n) {
                    spdlog::info("find_nearest_things: {} tiles visited", cnt);
                    return results;
                }
            }
        }

            for (const auto& [dx, dy] : directions) {
                try_enqueue(tile_x + dx, tile_y + dy);
            }
        }
    }

    // 未找到足够目标，返回已有结果
    return results;
}

std::vector<std::shared_ptr<RaceBase>> EcosystemState::find_nearest_races(
    const Position& center,
    const std::vector<std::string>& species_names,
    std::size_t n,
    double max_radius) const {
    ZoneScoped;
    if (!spatial_grid || n == 0 || max_radius < 0.0 || species_names.empty()) {
        return {};
    }

    const double cell_size = spatial_grid->get_cell_size();
    const int grid_width = spatial_grid->get_width();
    const int grid_height = spatial_grid->get_height();
    if (cell_size <= 0.0 || grid_width <= 0 || grid_height <= 0) {
        return {};
    }

    const double max_radius_sq = max_radius * max_radius;
    const std::unordered_set<std::string> species_filter(species_names.begin(), species_names.end());

    using Candidate = NearestCandidate<RaceBase>;
    std::priority_queue<Candidate, std::vector<Candidate>, FarthestFirst<RaceBase>> heap;

    const auto width_sz = static_cast<std::size_t>(grid_width);
    const auto height_sz = static_cast<std::size_t>(grid_height);
    std::vector<char> visited(width_sz * height_sz, 0);
    const auto index_for = [width_sz](int x, int y) {
        return static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x);
    };

    int start_x = static_cast<int>(std::floor(center.x / cell_size));
    int start_y = static_cast<int>(std::floor(center.y / cell_size));
    start_x = std::clamp(start_x, 0, grid_width - 1);
    start_y = std::clamp(start_y, 0, grid_height - 1);

    std::queue<std::pair<int, int>> frontier;
    const std::array<std::pair<int, int>, 8> directions{{
        std::make_pair(1, 0), std::make_pair(-1, 0), std::make_pair(0, 1), std::make_pair(0, -1),
        std::make_pair(1, 1), std::make_pair(1, -1), std::make_pair(-1, 1), std::make_pair(-1, -1)
    }};

    const auto try_enqueue = [&](int x, int y) {
        if (x < 0 || x >= grid_width || y < 0 || y >= grid_height) {
            return;
        }
        const auto idx = index_for(x, y);
        if (visited[idx]) {
            return;
        }

        const double cell_min_dist_sq = min_distance_sq_to_spatial_cell(center, x, y, cell_size);
        if (cell_min_dist_sq > max_radius_sq) {
            visited[idx] = 1;
            return;
        }

        if (heap.size() >= n) {
            const double current_far_sq = heap.top().distance_sq;
            if (cell_min_dist_sq > current_far_sq) {
                return;
            }
        }

        visited[idx] = 1;
        frontier.emplace(x, y);
    };

    try_enqueue(start_x, start_y);

    const auto& grid = spatial_grid->cells();
    while (!frontier.empty()) {
        const auto [cell_x, cell_y] = frontier.front();
        frontier.pop();

        const auto& bucket = grid[static_cast<std::size_t>(cell_x)][static_cast<std::size_t>(cell_y)];
        for (const auto& race : bucket) {
            if (!race || !race->alive) {
                continue;
            }
            if (species_filter.find(race->species_name) == species_filter.end()) {
                continue;
            }

            const double dist_sq = squared_distance(center, race->position);
            if (dist_sq > max_radius_sq) {
                continue;
            }

            if (heap.size() < n) {
                heap.push(Candidate{dist_sq, race});
            } else if (dist_sq < heap.top().distance_sq) {
                heap.pop();
                heap.push(Candidate{dist_sq, race});
            }
        }

        if (heap.size() >= n && heap.top().distance_sq <= 0.0) {
            break;
        }

        for (const auto& [dx, dy] : directions) {
            try_enqueue(cell_x + dx, cell_y + dy);
        }
    }

    std::vector<Candidate> ordered;
    ordered.reserve(heap.size());
    while (!heap.empty()) {
        ordered.push_back(heap.top());
        heap.pop();
    }

    std::sort(ordered.begin(), ordered.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.distance_sq < rhs.distance_sq;
    });

    std::vector<std::shared_ptr<RaceBase>> nearest;
    nearest.reserve(ordered.size());
    for (auto& entry : ordered) {
        if (entry.target) {
            nearest.push_back(std::move(entry.target));
        }
    }

    return nearest;
}

std::vector<std::shared_ptr<RaceBase>> EcosystemState::get_races_in_range(
    const std::string& species_name,
    const Position& center,
    double radius) const {
    return get_races_in_range(std::vector<std::string>{species_name}, center, radius);
}

std::vector<std::shared_ptr<ThingBase>> EcosystemState::get_things_in_range(
    const std::string& species_name,
    const Position& center,
    double radius) const {
    return get_things_in_range(std::vector<std::string>{species_name}, center, radius);
}