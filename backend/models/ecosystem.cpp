/*
生态系统数据模型
管理整个生态系统状态和数据 (C++ 迁移版本)
*/

#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <Eigen/Dense>
#include <spdlog/spdlog.h>
#include <cmath>
#include <limits>
#include <cassert>
#include <type_traits>
#include <iterator>

thread_local std::mt19937 EcosystemState::thread_local_rng{std::random_device{}()};
thread_local std::vector<InteractionRequest>* EcosystemState::tls_active_queue = nullptr;

// --- SpeciesType <-> string 映射函数 ---
SpeciesType species_type_from_name(const std::string& name) {
    if (name == "grass") return SpeciesType::GRASS;
    if (name == "cow") return SpeciesType::COW;
    if (name == "tiger") return SpeciesType::TIGER;
    throw std::invalid_argument("Unknown species name: " + name);
}

std::string name_from_species_type(SpeciesType type) {
    switch(type) {
        case SpeciesType::GRASS: return "grass";
        case SpeciesType::COW: return "cow";
        case SpeciesType::TIGER: return "tiger";
        default: return "";
    }
}

// --- SpeciesStatistics ---
// 物种统计管理 (种群跟踪)
SpeciesStatistics::SpeciesStatistics() {
    for (auto type : {SpeciesType::GRASS, SpeciesType::COW, SpeciesType::TIGER}) {
        statistics[type] = 0;
    }
}
void SpeciesStatistics::increment(SpeciesType type, int count) {
    statistics[type] += count;
}
void SpeciesStatistics::set_count(SpeciesType type, int count) {
    statistics[type] = count;
}
int SpeciesStatistics::get_count(SpeciesType type) const {
    auto it = statistics.find(type);
    return it != statistics.end() ? it->second : 0;
}
void SpeciesStatistics::reset() {
    for (auto& kv : statistics) kv.second = 0;
}
int SpeciesStatistics::grass() const { return get_count(SpeciesType::GRASS); }
void SpeciesStatistics::set_grass(int value) { set_count(SpeciesType::GRASS, value); }
int SpeciesStatistics::cow() const { return get_count(SpeciesType::COW); }
void SpeciesStatistics::set_cow(int value) { set_count(SpeciesType::COW, value); }
int SpeciesStatistics::tiger() const { return get_count(SpeciesType::TIGER); }
void SpeciesStatistics::set_tiger(int value) { set_count(SpeciesType::TIGER, value); }

// --- SpeciesRegistry ---
// 所有物种类型和个体的注册表
SpeciesRegistry::SpeciesRegistry(const EcosystemConfig& config) {
    {
        auto proto_unique = g_species_factory.create("grass", Position{0,0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("grass", proto, config.initial_grass);
    }
    {
        auto proto_unique = g_species_factory.create("cow", Position{0,0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("cow", proto, config.initial_cows);
    }
    {
        auto proto_unique = g_species_factory.create("tiger", Position{0,0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("tiger", proto, config.initial_tigers);
    }
}
void SpeciesRegistry::register_species(const std::string& name, std::shared_ptr<Species> prototype, int initial_count) {
    registry[name] = SpeciesInfo{name, {}, initial_count};
}
std::vector<std::shared_ptr<Species>>& SpeciesRegistry::get_species_list(const std::string& name) {
    return registry[name].list;
}
const std::vector<std::shared_ptr<Species>>& SpeciesRegistry::get_species_list(const std::string& name) const {
    return registry.at(name).list;
}
int SpeciesRegistry::get_initial_count(const std::string& name) const {
    auto it = registry.find(name);
    return it != registry.end() ? it->second.initial_count : 0;
}
std::vector<std::string> SpeciesRegistry::get_all_species_names() const {
    std::vector<std::string> names;
    for (const auto& kv : registry) names.push_back(kv.first);
    return names;
}
void SpeciesRegistry::add_individual(const std::string& name, std::shared_ptr<Species> individual) {
    registry[name].list.push_back(individual);
}
void SpeciesRegistry::extend_individuals(const std::string& name, const std::vector<std::shared_ptr<Species>>& individuals) {
    auto& list = registry[name].list;
    list.insert(list.end(), individuals.begin(), individuals.end());
}
void SpeciesRegistry::clear_species(const std::string& name) {
    registry[name].list.clear();
}
void SpeciesRegistry::clear_all() {
    for (auto& kv : registry) kv.second.list.clear();
}
int SpeciesRegistry::get_species_count(const std::string& name) const {
    auto it = registry.find(name);
    return it != registry.end() ? it->second.list.size() : 0;
}
int SpeciesRegistry::get_total_count() const {
    int sum = 0;
    for (const auto& kv : registry) sum += kv.second.list.size();
    return sum;
}
void SpeciesRegistry::filter_alive(const std::string& name) {
    auto& list = registry[name].list;
    list.erase(std::remove_if(list.begin(), list.end(),
        [](const std::shared_ptr<Species>& s){ return !s->alive; }), list.end());
}
void SpeciesRegistry::filter_all_alive() {
    for (auto& kv : registry) filter_alive(kv.first);
}

// --- EcosystemState ---
// 生态系统状态管理器 (模拟核心)
EcosystemState::EcosystemState(const EcosystemConfig& config)
    : config(config), time_step(0), species_registry(config), births(), deaths(), population_history() {
    // --- 均匀网格初始化 ---
    // 选择一个合适的单元格尺寸，后续可根据物种参数调整
    cell_size = 100.0;
    grid_width = static_cast<int>(std::ceil(static_cast<double>(config.world_width) / cell_size));
    grid_height = static_cast<int>(std::ceil(static_cast<double>(config.world_height) / cell_size));

    // 调整网格大小以匹配维度（每个单元格为一个 Species 指针列表）
    spatial_grid.resize(static_cast<size_t>(grid_width),
        std::vector<std::vector<std::shared_ptr<Species>>>(static_cast<size_t>(grid_height))
    );

    initialize_populations();
}

/*
使用统一逻辑初始化所有物种的种群
*/
void EcosystemState::initialize_populations() {
    for (const auto& name : species_registry.get_all_species_names()) {
        int initial_count = species_registry.get_initial_count(name);
        for (int i = 0; i < initial_count; ++i) {
            int x = rand() % config.world_width;
            int y = rand() % config.world_height;
            // 使用工厂模式创建物种实例
            std::shared_ptr<Species> individual = g_species_factory.create(name, Position{(double)x, (double)y});
            species_registry.add_individual(name, individual);
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
    state.current_day = get_current_day();
    state.current_quadrum = get_current_quadrum();
    state.current_year = get_current_year();
    state.current_quadrum_name = get_current_quadrum_name();

    // 填充species_lists map
    for (const auto& species_name : species_registry.get_all_species_names()) {
        state.species_lists[species_name] = species_registry.get_species_list(species_name);
    }

    // 预计算草的位置和存活对象 (Eigen矩阵)
    std::vector<std::shared_ptr<Species>> alive_grass_objects;
    std::vector<Eigen::Vector2d> alive_grass_positions;
    
    auto grass_it = state.species_lists.find("grass");
    if (grass_it != state.species_lists.end()) {
        for (const auto& grass : grass_it->second) {
            if (grass->alive) {
                alive_grass_objects.push_back(grass);
                alive_grass_positions.emplace_back(grass->position.x, grass->position.y);
            }
        }
    }
    
    state.alive_grass_objects = alive_grass_objects;
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
void EcosystemState::update_time() {
    // --- 时间推进与计算 ---
    time_step++; // tick 递增
}

/*
更新统计信息并维护种群历史
*/
void EcosystemState::update_statistics() {
    SpeciesStatistics stats = get_species_counts();
    std::map<SpeciesType, int> snapshot = stats.statistics;
    population_history.push_back(snapshot);
    if (population_history.size() > 100)
        population_history.erase(population_history.begin(), population_history.end() - 100);
}

/**
 * @brief 准备进行新一轮的并发更新。
 *
 * 此函数在每个模拟步骤的开始被调用，用于清理和重置与并发更新相关的状态。
 * 主要包括：
 * 1. 清空空间哈希网格，为重新构建索引做准备。
 * 2. 清理上一轮的交互请求、能量变更、死亡标记和出生标记。
 */
void EcosystemState::prepare_for_update() {
    // TODO(阶段后续): 基于均匀网格构建空间索引，取代旧的四叉树方案。
    for (auto& column : spatial_grid) {
        for (auto& cell : column) {
            cell.clear();
        }
    }

    staged_requests.clear();
    main_thread_requests.clear();
    energy_changes.clear();
    marked_for_death.clear();
    marked_for_birth.clear();
}

/**
 * @brief 将所有物种的决策任务分派到线程池中并行执行。
 *
 * 此函数是并发更新的第一阶段（决策阶段）。它将每个物种的更新（决策）任务
 * 分割成小块（chunk），并提交到线程池中。每个任务都会在一个单独的线程中
 * 执行物种的 `update`（未来将是 `decide`）方法。
 *
 * 为了实现无锁的交互请求收集，每个工作线程都会被分配一个专属的请求队列。
 * `activate_request_queue` 和 `restore_request_queue` 用于管理当前线程
 * 正在使用的队列，确保线程安全。
 *
 * @param pool 要使用的线程池。
 */
void EcosystemState::dispatch_decision_tasks(ThreadPool& pool) {
    constexpr std::size_t chunk_size = 512;

    const std::size_t worker_count = std::max<std::size_t>(1, pool.worker_count());
    if (worker_request_queues.size() != worker_count) {
        worker_request_queues.assign(worker_count, {});
    }
    for (auto& queue : worker_request_queues) {
        queue.clear();
    }

    const auto species_names = species_registry.get_all_species_names();
    for (const auto& name : species_names) {
        auto& list = species_registry.get_species_list(name);
        if (list.empty()) continue;

        if (list.size() <= chunk_size) {
            pool.submit([this, &list] {
                const auto worker_index = ThreadPool::current_worker_index();
                auto* previous_queue = activate_request_queue(worker_index < worker_request_queues.size()
                    ? &worker_request_queues[worker_index]
                    : nullptr);
                for (auto& individual : list) {
                    if (individual->alive) {
                        individual->update(*this); // TODO: replace with decide() once available
                    }
                }
                restore_request_queue(previous_queue);
            });
            continue;
        }

        for (std::size_t offset = 0; offset < list.size(); offset += chunk_size) {
            const std::size_t start = offset;
            const std::size_t end = std::min(offset + chunk_size, list.size());
            pool.submit([this, &list, start, end] {
                const auto worker_index = ThreadPool::current_worker_index();
                auto* previous_queue = activate_request_queue(worker_index < worker_request_queues.size()
                    ? &worker_request_queues[worker_index]
                    : nullptr);
                for (std::size_t i = start; i < end; ++i) {
                    auto& individual = list[i];
                    if (individual->alive) {
                        individual->update(*this); // TODO: replace with decide() once available
                    }
                }
                restore_request_queue(previous_queue);
            });
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
    staged_requests.clear();
    for (auto& queue : worker_request_queues) {
        if (!queue.empty()) {
            staged_requests.insert(staged_requests.end(),
                                   std::make_move_iterator(queue.begin()),
                                   std::make_move_iterator(queue.end()));
            queue.clear();
        }
    }

    if (!main_thread_requests.empty()) {
        staged_requests.insert(staged_requests.end(),
                               std::make_move_iterator(main_thread_requests.begin()),
                               std::make_move_iterator(main_thread_requests.end()));
        main_thread_requests.clear();
    }

    if (staged_requests.empty()) {
        return;
    }

    for (auto& request : staged_requests) {
        std::visit([this](auto&& req) {
            using RequestType = std::decay_t<decltype(req)>;
            if constexpr (std::is_same_v<RequestType, AttemptToEatRequest>) {
                auto& initiator = req.initiator;
                auto& target = req.target;
                if (!initiator || !target) return;
                if (!initiator->alive || !target->alive) return;

                if (marked_for_death.contains(target.get())) return;

                marked_for_death.insert(target.get());
                energy_changes[initiator.get()] += target->energy;
            } else if constexpr (std::is_same_v<RequestType, AttemptToReproduceRequest>) {
                auto& parent = req.parent;
                if (!parent || !parent->alive) return;
                marked_for_birth.push_back(parent->position);
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
    (void)pool; // 阶段 1：暂未引入并发应用逻辑，预留接口
}

/**
 * @brief 应用所有在更新周期中累积的注册表变更。
 *
 * 此函数是更新周期的最后阶段，负责处理物种的出生和死亡。
 * 它会遍历所有物种，处理繁殖请求（创建新个体），并根据 `marked_for_death`
 * 集合移除死亡的个体。同时，它也会应用累积的能量变化，并更新统计数据。
 *
 * 将这些变更放在最后统一处理，可以避免在迭代物种列表时修改它，从而简化
 * 并发控制和逻辑。
 */
void EcosystemState::apply_registry_changes() {
    // 处理阶段 3/4 的累积结果（目前保持原有串行逻辑）
    for (const auto& name : species_registry.get_all_species_names()) {
        auto& list = species_registry.get_species_list(name);

    std::vector<std::shared_ptr<Species>> new_individuals;
    new_individuals.reserve(list.size() / 2);

        for (auto& individual : list) {
            if (individual->can_reproduce()) {
                auto offspring = individual->reproduce(*this);
                if (offspring) new_individuals.push_back(std::move(offspring));
            }
        }

        if (!new_individuals.empty()) {
            species_registry.extend_individuals(name, new_individuals);
            SpeciesType type = species_type_from_name(name);
            births.increment(type, new_individuals.size());
            spdlog::get("ecosim")->info("{} {} new {} individuals born",
                (name == "grass" ? "🌱" : name == "cow" ? "🐄" : "🐅"),
                new_individuals.size(), name);
        }

        int dead_count = 0;
        for (auto& individual : list) {
            if (!individual->alive || marked_for_death.contains(individual.get())) {
                individual->alive = false;
                ++dead_count;
            } else {
                auto energy_it = energy_changes.find(individual.get());
                if (energy_it != energy_changes.end()) {
                    individual->energy += energy_it->second;
                }
            }
        }
        if (dead_count > 0) {
            SpeciesType type = species_type_from_name(name);
            deaths.increment(type, dead_count);
            spdlog::get("ecosim")->info("💀 {} {} individuals died", dead_count, name);
        }

        species_registry.filter_alive(name);
    }

    marked_for_death.clear();
    energy_changes.clear();
    staged_requests.clear();
    marked_for_birth.clear();
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
    if (tls_active_queue) {
        tls_active_queue->push_back(std::move(request));
    } else {
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

/*
获取所有物种的当前种群数量
*/
SpeciesStatistics EcosystemState::get_species_counts() const {
    SpeciesStatistics stats;
    for (const auto& name : species_registry.get_all_species_names()) {
        SpeciesType type = species_type_from_name(name);
        int count = species_registry.get_species_count(name);
        stats.set_count(type, count);
    }
    return stats;
}

/*
获取所有物种的详细数据 (用于前端/统计)
*/
SpeciesPopulationData EcosystemState::get_species_data() const {
    SpeciesPopulationData data;
    for (const auto& name : species_registry.get_all_species_names()) {
        const auto& list = species_registry.get_species_list(name);
        std::vector<BaseIndividualData> individuals;
        for (const auto& individual : list) {
            if (individual->alive) {
                BaseIndividualData ind;
                ind.id = reinterpret_cast<std::uintptr_t>(individual.get()); // 使用地址作为id (C++ 迁移)
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

/*
使用统一逻辑将生态系统重置为初始状态
*/
void EcosystemState::reset(const EcosystemConfig& new_config) {
    config = new_config;
    time_step = 0;
    species_registry.clear_all();
    births.reset();
    deaths.reset();
    population_history.clear();
    initialize_populations();
}

/*
检查并返回已灭绝的物种
*/
std::vector<std::string> EcosystemState::check_extinction() const {
    std::vector<std::string> extinct;
    for (const auto& name : species_registry.get_all_species_names()) {
        if (species_registry.get_species_count(name) == 0)
            extinct.push_back(name);
    }
    return extinct;
}

/*
通用查询接口：获取指定范围内的物种个体
*/
std::vector<std::shared_ptr<Species>> EcosystemState::get_species_in_range(
    const std::string& species_name, 
    const Position& center, 
    double radius) const {
    
    std::vector<std::shared_ptr<Species>> result;
    
    // 检查物种是否存在
    auto it = species_registry.registry.find(species_name);
    if (it == species_registry.registry.end()) {
        return result; // 返回空列表
    }
    
    // 遍历该物种的所有个体
    const auto& species_list = it->second.list;
    for (const auto& individual : species_list) {
        // 检查个体是否存活且在指定范围内
        if (individual->alive && 
            individual->position.distance_to(center) <= radius) {
            result.push_back(individual);
        }
    }
    
    return result;
}