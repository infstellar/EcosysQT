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
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

thread_local std::mt19937 EcosystemState::thread_local_rng{std::random_device{}()};
thread_local std::vector<InteractionRequest>* EcosystemState::tls_active_queue = nullptr;

// --- EcosystemState ---
// 生态系统状态管理器 (模拟核心)
EcosystemState::EcosystemState(const EcosystemConfig& config)
            : config(config),
                time_step(0),
                delta_ticks(1.0),
                races_registry(config),
                births(),
                deaths(),
                population_history(),
                spatial_grid(std::make_unique<SpatialGrid>(config.world_width, config.world_height, 100.0)),
                m_world_grid(static_cast<std::size_t>(std::max(0, config.world_width)) *
                                         static_cast<std::size_t>(std::max(0, config.world_height))),
                m_all_things(),
                thing_reproduction_parents() {
    initialize_populations();
}

/*
使用统一逻辑初始化所有物种的种群
*/
void EcosystemState::initialize_populations() {
    auto logger = spdlog::get("ecosim");
    const auto expected_size = static_cast<std::size_t>(std::max(0, config.world_width)) *
                               static_cast<std::size_t>(std::max(0, config.world_height));
    if (m_world_grid.size() != expected_size) {
        m_world_grid.assign(expected_size, Tile{});
    }
    for (auto& tile : m_world_grid) {
        tile.things.clear();
    }
    m_all_things.clear();

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
            // 使用 get_thread_local_rng() 保证高质量随机数
            std::uniform_real_distribution<> distX(0, config.world_width);
            std::uniform_real_distribution<> distY(0, config.world_height);
            int x = distX(get_thread_local_rng());
            int y = distY(get_thread_local_rng());
            try {
                // 调用工厂时，传入 get_thread_local_rng()
                auto new_individual = g_race_factory.create(name, Position{static_cast<double>(x), static_cast<double>(y)}, get_thread_local_rng());
                races_registry.add_individual(name, std::move(new_individual));
            } catch (const std::exception& e) {
                if (logger) {
                    logger->error("[Init] Failed to create instance for '{}' at index {}: {}", name, i, e.what());
                }
                throw; // 上层捕获并报告
            }
        }
    }

    auto thing_names = g_thing_factory.get_all_species_names();
    if (logger) {
        logger->info("[Init] Initializing things for {} species", thing_names.size());
    }
    if (config.world_width <= 0 || config.world_height <= 0) {
        if (logger) {
            logger->warn("[Init] World dimensions are non-positive; skipping thing initialization");
        }
        return;
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
            constexpr int kMaxPlacementAttempts = 16;
            bool placed = false;
            while (!placed && attempts < kMaxPlacementAttempts * initial_count) {
                ++attempts;
                int tile_x = dist_tile_x(rng);
                int tile_y = dist_tile_y(rng);
                Tile& tile = get_tile(tile_x, tile_y);
                if (tile.biome != BiomeType::LAND || !tile.things.empty()) {
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
}

/*
使用getter函数算出时间
*/
int EcosystemState::get_current_day() const {
    return (time_step / 30000) + 1;
}

int EcosystemState::get_current_year() const {
    return ((get_current_day() - 1) / 60) + 1;
}

int EcosystemState::get_current_quadrum() const {
    int day_of_year = ((get_current_day() - 1) % 60) + 1;
    return ((day_of_year - 1) / 15) + 1;
}

std::string EcosystemState::get_current_quadrum_name() const {
    static const char* quadrum_names[] = {"Aprimay", "Jugust", "Septober", "Decembery"};
    int quadrum_index = get_current_quadrum() - 1;
    if (quadrum_index >= 0 && quadrum_index < 4) {
        return quadrum_names[quadrum_index];
    }
    spdlog::get("ecosim")->warn("get_current_quadrum_name 返回了未知值，请检查时间计算逻辑");
    return "Unknown"; // 安全保护
}

/*
获取用于模拟和前端的生态系统状态快照
*/
EcosystemStateData EcosystemState::get_ecosystem_state() const {
    EcosystemStateData state;
    state.world_width = config.world_width;
    state.world_height = config.world_height;
    state.time_step = time_step;
    state.delta_ticks = delta_ticks;
    state.current_day = get_current_day();
    state.current_quadrum = get_current_quadrum();
    state.current_year = get_current_year();
    state.current_quadrum_name = get_current_quadrum_name();

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

    for (const auto& thing : m_all_things) {
        if (!thing) {
            continue;
        }
        state.thing_lists[thing->species_name].push_back(thing);
    }

    // 预计算草的位置和存活对象 (Eigen矩阵)
    std::vector<Eigen::Vector2d> alive_grass_positions;

    auto grass_it = state.thing_lists.find("grass");
    if (grass_it != state.thing_lists.end()) {
        for (const auto& grass : grass_it->second) {
            if (grass && grass->alive) {
                state.alive_grass_objects.push_back(grass);
                alive_grass_positions.emplace_back(grass->position.x, grass->position.y);
            }
        }
    }

    if (!alive_grass_positions.empty()) {
        state.grass_positions_array = Eigen::MatrixXd(alive_grass_positions.size(), 2);
        for (size_t i = 0; i < alive_grass_positions.size(); ++i) {
            state.grass_positions_array(i, 0) = alive_grass_positions[i](0);
            state.grass_positions_array(i, 1) = alive_grass_positions[i](1);
        }
    } else {
        state.grass_positions_array = Eigen::MatrixXd(0, 2);
    }
    return state;
}

/*
使用统一逻辑更新时间状态
*/
void EcosystemState::update_time_ticks(double delta_ticks_param) {
    // --- 时间推进与计算（tick制） ---
    delta_ticks = delta_ticks_param;
    time_step++; // 维持离散步计数（整数），与tick小数独立
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

std::size_t EcosystemState::get_grid_index(int x, int y) const {
    if (!is_valid_grid_coord(x, y)) {
        throw std::out_of_range("Grid coordinate out of range");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(config.world_width) + static_cast<std::size_t>(x);
}

bool EcosystemState::is_valid_grid_coord(int x, int y) const {
    return x >= 0 && x < config.world_width && y >= 0 && y < config.world_height;
}

Tile& EcosystemState::get_tile(int x, int y) {
    if (!is_valid_grid_coord(x, y)) {
        throw std::out_of_range("Grid coordinate out of range");
    }
    return m_world_grid[get_grid_index(x, y)];
}

const Tile& EcosystemState::get_tile(int x, int y) const {
    if (!is_valid_grid_coord(x, y)) {
        throw std::out_of_range("Grid coordinate out of range");
    }
    return m_world_grid[get_grid_index(x, y)];
}

void EcosystemState::attach_thing_to_world(const std::shared_ptr<ThingBase>& thing) {
    if (!thing) {
        return;
    }
    if (!is_valid_grid_coord(thing->m_grid_x, thing->m_grid_y)) {
        throw std::out_of_range("Thing grid coordinate out of range");
    }
    Tile& tile = get_tile(thing->m_grid_x, thing->m_grid_y);
    tile.things.push_back(thing.get());
    m_all_things.push_back(thing);
}

void EcosystemState::detach_thing_from_tile(ThingBase& thing) {
    if (!is_valid_grid_coord(thing.m_grid_x, thing.m_grid_y)) {
        return;
    }
    Tile& tile = get_tile(thing.m_grid_x, thing.m_grid_y);
    auto it = std::remove(tile.things.begin(), tile.things.end(), &thing);
    if (it != tile.things.end()) {
        tile.things.erase(it, tile.things.end());
    }
    thing.m_grid_x = -1;
    thing.m_grid_y = -1;
}

void EcosystemState::update_things() {
    auto& rng = get_thread_local_rng();
    for (auto& thing : m_all_things) {
        if (!thing || !thing->alive) {
            continue;
        }
        thing->decide(*this, rng);
        thing->apply(*this);
    }
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
    staged_requests.clear();      // 清空暂存的交互请求
    main_thread_requests.clear(); // 清空主线程处理的请求
    race_energy_changes.clear();  // 清空能量变化记录
    race_marked_for_death.clear();     // 清空待移除的生物体列表
    thing_energy_changes.clear();
    thing_marked_for_death.clear();
    reproduction_parents.clear(); // 清空待新生的父代列表
    thing_reproduction_parents.clear();

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
    // 定义每个任务处理的个体数量。选择一个较大的值可以减少任务创建的开销，
    // 但也可能导致负载不均。1024 是一个在开销和负载均衡之间的合理权衡。
    constexpr std::size_t chunk_size = 1024;

    // 确保 `worker_request_queues` 的大小与线程池的工作线程数一致。
    // 如果不一致（例如，线程池大小在运行时发生变化），则重新分配队列。
    const std::size_t worker_count = std::max<std::size_t>(1, pool.worker_count());
    if (worker_request_queues.size() != worker_count) {
        worker_request_queues.assign(worker_count, {});
    }
    // 在新一轮决策开始前，清空所有线程的请求队列。
    for (auto& queue : worker_request_queues) {
        queue.clear();
    }

    // 定义一个 lambda 函数，用于将一部分个体（一个“块”）的决策任务提交到线程池。
    const auto submit_chunk = [this, &pool](std::vector<std::shared_ptr<RaceBase>>& list,
                                            std::size_t begin,
                                            std::size_t end) {
        // 向线程池提交一个新任务。
        pool.submit([this, &list, begin, end] {
            // 获取当前工作线程的索引，以便找到对应的请求队列。
            const auto worker_index = ThreadPool::current_worker_index();
            std::vector<InteractionRequest>* active_queue = nullptr;
            // 确保工作索引在有效范围内，然后获取该线程的请求队列指针。
            if (worker_index < worker_request_queues.size()) {
                active_queue = &worker_request_queues[worker_index];
            }

            // 激活当前线程的请求队列。`submit_interaction_request` 将把请求放入此队列。
            // `activate_request_queue` 返回先前的队列，以便在任务结束时恢复。
            auto* previous_queue = activate_request_queue(active_queue);
            // 遍历分配给该任务的个体。
            auto& rng = get_thread_local_rng();
            for (std::size_t i = begin; i < end; ++i) {
                auto& individual = list[i];
                if (!individual) {
                    continue;
                }
                individual->decide(*this, rng);
            }
            // 任务完成，恢复之前的请求队列。这对于嵌套任务或单线程回退情况很重要。
            restore_request_queue(previous_queue);
        });
    };

    // 遍历所有已注册的物种，为它们分派决策任务。
    const auto species_names = races_registry.get_all_species_names();
    for (const auto& name : species_names) {
        auto& list = races_registry.get_species_list(name);
        if (list.empty()) {
            continue;
        }

        // 如果个体数量小于或等于块大小，则直接提交一个任务。
        if (list.size() <= chunk_size) {
            submit_chunk(list, 0, list.size());
            continue;
        }

        // 如果个体数量大于块大小，则分块提交任务。
        for (std::size_t begin = 0; begin < list.size(); begin += chunk_size) {
            const std::size_t end = std::min(begin + chunk_size, list.size());
            submit_chunk(list, begin, end);
        }
    }
}

/**
 * @brief 解决在决策阶段产生的所有交互请求。
 *
 * 此函数是并发更新的第二阶段（交互解决阶段）。它首先将所有工作线程的
 * 本地请求队列中的请求移动到一个统一的 `staged_requests` 队列中，
 * 然后遍历这些请求，并根据请求类型（如捕食、繁殖）更新相关的状态
 * （如标记死亡、记录能量变化、标记出生）。
 *
 * 这是一个同步点，确保在进入下一阶段（应用阶段）之前，所有交互都已解决。
 */
void EcosystemState::resolve_interactions() {
    // 清空上一轮的暂存请求。
    staged_requests.clear();
    // 将所有工作线程的本地请求队列中的请求移动到统一的 `staged_requests` 队列中。
    // 使用 `std::make_move_iterator` 可以高效地转移请求，避免不必要的拷贝。
    for (auto& queue : worker_request_queues) {
        if (!queue.empty()) {
            staged_requests.insert(staged_requests.end(),
                                   std::make_move_iterator(queue.begin()),
                                   std::make_move_iterator(queue.end()));
            queue.clear();
        }
    }

    // 如果主线程（或单线程模式）也有请求，同样移入暂存队列。
    if (!main_thread_requests.empty()) {
        staged_requests.insert(staged_requests.end(),
                               std::make_move_iterator(main_thread_requests.begin()),
                               std::make_move_iterator(main_thread_requests.end()));
        main_thread_requests.clear();
    }

    // 如果没有需要处理的请求，则提前返回。
    if (staged_requests.empty()) {
        return;
    }

    // 遍历所有暂存的请求，并根据其类型进行处理。
    for (auto& request : staged_requests) {
        // 使用 `std::visit` 和 `std::variant` 来处理不同类型的请求。
        std::visit([this](auto&& req) {
            using RequestType = std::decay_t<decltype(req)>;
            if constexpr (std::is_same_v<RequestType, AttemptToEatRaceRequest>) {
                auto& initiator = req.initiator;
                auto& target = req.target;
                if (!initiator || !target) return;
                if (!initiator->alive || !target->alive) return;

                if (race_marked_for_death.find(target.get()) != race_marked_for_death.end()) return;

                race_marked_for_death.insert(target.get());
                race_energy_changes[initiator.get()] += target->energy;
                target->die_from_predation(initiator->species_name);
            } else if constexpr (std::is_same_v<RequestType, AttemptToEatThingRequest>) {
                auto& initiator = req.initiator;
                auto& target = req.target;
                if (!initiator || !target) return;
                if (!initiator->alive || !target->alive) return;

                if (thing_marked_for_death.find(target.get()) != thing_marked_for_death.end()) return;

                thing_marked_for_death.insert(target.get());
                race_energy_changes[initiator.get()] += target->energy;
                target->die_from_predation(initiator->species_name);
            } else if constexpr (std::is_same_v<RequestType, AttemptToReproduceRaceRequest>) {
                if (req.parent && req.parent->alive) {
                    reproduction_parents.push_back(std::move(req.parent));
                }
            } else if constexpr (std::is_same_v<RequestType, AttemptToReproduceThingRequest>) {
                if (req.parent && req.parent->alive) {
                    thing_reproduction_parents.push_back(std::move(req.parent));
                }
            } else if constexpr (std::is_same_v<RequestType, AttemptToMateRequest>) {
                auto& female = req.female;
                auto& male = req.male;

                if (female && male && female->alive && male->alive &&
                    female->can_reproduce() && male->mating_timer <= 0)
                {
                    female->begin_mating_with(male);
                    male->begin_mating_with(female);
                    female->become_pregnant();
                    male->start_reproduction_cooldown();
                    female->energy -= female->reproduction_energy_cost;
                    male->energy -= male->reproduction_energy_cost;
                }
            }
        }, request);
    }
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
    constexpr std::size_t chunk_size = 1024;

    const auto submit_chunk = [this, &pool](std::vector<std::shared_ptr<RaceBase>>& list,
                                            std::size_t begin,
                                            std::size_t end) {
        pool.submit([this, &list, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                auto& individual = list[i];
                if (!individual) {
                    continue;
                }
                individual->apply(*this);
            }
        });
    };

    const auto species_names = races_registry.get_all_species_names();
    for (const auto& name : species_names) {
        auto& list = races_registry.get_species_list(name);
        if (list.empty()) {
            continue;
        }

        if (list.size() <= chunk_size) {
            submit_chunk(list, 0, list.size());
            continue;
        }

        for (std::size_t begin = 0; begin < list.size(); begin += chunk_size) {
            const std::size_t end = std::min(begin + chunk_size, list.size());
            submit_chunk(list, begin, end);
        }
    }
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
    // --- 阶段 3/4：应用变更 --- 
    // 遍历所有物种，处理繁殖、死亡和能量变化。

    // 用于临时存储本轮出生的新个体。
    std::unordered_map<std::string, std::vector<std::shared_ptr<RaceBase>>> newborns_by_species;
    newborns_by_species.reserve(reproduction_parents.size());
    std::unordered_map<std::string, int> thing_birth_counts;
    std::unordered_map<std::string, int> thing_death_counts;
    auto logger = spdlog::get("ecosim");

    // --- 出生处理 ---
    for (auto& parent : reproduction_parents) {
        if (!parent || !parent->alive) {
            continue;
        }

        const auto spawn_position = parent->consume_pending_spawn_position();
        if (!spawn_position.has_value()) {
            continue;
        }

        // 调用工厂时，传入 get_thread_local_rng()
        auto offspring_unique = g_race_factory.create(parent->species_name, spawn_position.value(), get_thread_local_rng());
        if (!offspring_unique) {
            continue;
        }

        std::shared_ptr<RaceBase> offspring = std::move(offspring_unique);
        offspring->position = spawn_position.value();
        newborns_by_species[parent->species_name].push_back(std::move(offspring));
    }

    for (const auto& name : races_registry.get_all_species_names()) {
        auto& list = races_registry.get_species_list(name);

        int dead_count = 0;
        for (auto& individual : list) {
            if (!individual) {
                continue;
            }

            if (race_marked_for_death.find(individual.get()) != race_marked_for_death.end()) {
                if (individual->alive) {
                    individual->alive = false;
                    ++dead_count;
                }
                continue;
            }

            if (auto energy_it = race_energy_changes.find(individual.get());
                energy_it != race_energy_changes.end()) {
                individual->energy += energy_it->second;
            }
        }

        if (dead_count > 0) {
            deaths.increment(name, dead_count);
            spdlog::get("ecosim")->info("💀 {} {} individuals died", dead_count, name);
        }

        races_registry.filter_alive(name);

        auto newborn_it = newborns_by_species.find(name);
        if (newborn_it != newborns_by_species.end() && !newborn_it->second.empty()) {
            races_registry.extend_individuals(name, newborn_it->second);
            births.increment(name, static_cast<int>(newborn_it->second.size()));
            spdlog::get("ecosim")->info("{} {} new {} individuals born",
                (name == "grass" ? "🌱" : name == "cow" ? "🐄" : "🐅"),
                newborn_it->second.size(), name);
        }
    }

    auto remove_it = std::remove_if(m_all_things.begin(), m_all_things.end(),
        [this, &thing_death_counts](const std::shared_ptr<ThingBase>& thing) {
            if (!thing) {
                return true;
            }
            if (thing->alive) {
                return false;
            }
            ++thing_death_counts[thing->species_name];
            detach_thing_from_tile(*thing);
            return true;
        });
    m_all_things.erase(remove_it, m_all_things.end());

    if (config.world_width > 0 && config.world_height > 0) {
        const int max_x = config.world_width - 1;
        const int max_y = config.world_height - 1;
        for (auto& parent : thing_reproduction_parents) {
            if (!parent || !parent->alive) {
                continue;
            }
            const auto spawn_position = parent->consume_pending_spawn_position();
            if (!spawn_position.has_value()) {
                continue;
            }

            int tile_x = static_cast<int>(std::floor(spawn_position->x));
            int tile_y = static_cast<int>(std::floor(spawn_position->y));
            tile_x = std::clamp(tile_x, 0, max_x);
            tile_y = std::clamp(tile_y, 0, max_y);

            if (!is_valid_grid_coord(tile_x, tile_y)) {
                continue;
            }

            Tile& tile = get_tile(tile_x, tile_y);
            const bool tile_available = std::none_of(tile.things.begin(), tile.things.end(),
                [](ThingBase* existing) {
                    return existing != nullptr && existing->alive;
                });
            if (!tile_available) {
                continue;
            }

            Position world_pos{static_cast<double>(tile_x) + 0.5, static_cast<double>(tile_y) + 0.5};
            auto offspring_unique = g_thing_factory.create(parent->species_name, world_pos, get_thread_local_rng());
            if (!offspring_unique) {
                continue;
            }

            std::shared_ptr<ThingBase> offspring(std::move(offspring_unique));
            offspring->position = world_pos;
            offspring->m_grid_x = tile_x;
            offspring->m_grid_y = tile_y;
            attach_thing_to_world(offspring);
            ++thing_birth_counts[parent->species_name];
        }
    }

    for (const auto& [species, count] : thing_birth_counts) {
        if (count <= 0) {
            continue;
        }
        births.increment(species, count);
        if (logger) {
            logger->info("🌱 {} new {} things born", count, species);
        }
    }

    for (const auto& [species, count] : thing_death_counts) {
        if (count <= 0) {
            continue;
        }
        deaths.increment(species, count);
        if (logger) {
            logger->info("🥀 {} {} things removed", count, species);
        }
    }

    thing_reproduction_parents.clear();

    // --- 清理状态 ---
    // 清理本轮的状态标记，为下一轮更新做准备。
    race_marked_for_death.clear();
    thing_marked_for_death.clear();
    race_energy_changes.clear();
    thing_energy_changes.clear();
    staged_requests.clear();
    reproduction_parents.clear();
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
    time_step = 0;
    races_registry.clear_all();
    births.reset();
    deaths.reset();
    population_history.clear();
    // 清理并发更新相关的状态
    staged_requests.clear();
    main_thread_requests.clear();
    race_energy_changes.clear();
    race_marked_for_death.clear();
    thing_energy_changes.clear();
    thing_marked_for_death.clear();
    reproduction_parents.clear();
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
    std::vector<std::shared_ptr<ThingBase>> nearby;
    if (radius < 0.0) {
        return nearby;
    }

    const double radius_sq = radius * radius;
    nearby.reserve(m_all_things.size());
    for (const auto& thing : m_all_things) {
        if (!thing || !thing->alive) {
            continue;
        }
        const double dx = thing->position.x - center.x;
        const double dy = thing->position.y - center.y;
        if ((dx * dx + dy * dy) <= radius_sq) {
            nearby.push_back(thing);
        }
    }

    return nearby;
}

std::vector<std::shared_ptr<RaceBase>> EcosystemState::get_races_in_range(
    const std::string& species_name,
    const Position& center,
    double radius) const {
    std::vector<std::shared_ptr<RaceBase>> result;
    if (!races_registry.has_species(species_name)) {
        return result;
    }

    const auto& list = races_registry.get_species_list(species_name);
    const double radius_sq = radius * radius;
    result.reserve(list.size());
    for (const auto& individual : list) {
        if (!individual || !individual->alive) {
            continue;
        }
        const double dx = individual->position.x - center.x;
        const double dy = individual->position.y - center.y;
        if ((dx * dx + dy * dy) <= radius_sq) {
            result.push_back(individual);
        }
    }

    return result;
}

std::vector<std::shared_ptr<ThingBase>> EcosystemState::get_things_in_range(
    const std::string& species_name,
    const Position& center,
    double radius) const {
    std::vector<std::shared_ptr<ThingBase>> result;
    const double radius_sq = radius * radius;
    result.reserve(m_all_things.size());

    for (const auto& thing : m_all_things) {
        if (!thing || !thing->alive) {
            continue;
        }
        if (thing->species_name != species_name) {
            continue;
        }
        const double dx = thing->position.x - center.x;
        const double dy = thing->position.y - center.y;
        if ((dx * dx + dy * dy) <= radius_sq) {
            result.push_back(thing);
        }
    }

    return result;
}