/*
生态系统数据模型
管理整个生态系统状态和数据 (C++ 迁移版本)
*/

#include "ecosystem.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <Eigen/Dense>

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
获取用于模拟和前端的生态系统状态快照
*/
EcosystemStateData EcosystemState::get_ecosystem_state() const {
    EcosystemStateData state;
    state.world_width = config.world_width;
    state.world_height = config.world_height;
    state.time_step = time_step;

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
使用统一逻辑更新所有物种
*/
void EcosystemState::update_species() {
    for (const auto& name : species_registry.get_all_species_names()) {
        auto& list = species_registry.get_species_list(name);
        for (auto& individual : list) {
            individual->update(*this);
        }
    }
}


/*
使用统一逻辑处理所有物种的繁殖
*/
void EcosystemState::handle_reproduction() {
    for (const auto& name : species_registry.get_all_species_names()) {
        auto& list = species_registry.get_species_list(name);
        std::vector<std::shared_ptr<Species>> new_individuals;
        for (auto& individual : list) {
            if (individual->can_reproduce()) {
                auto offspring = individual->reproduce(*this); // 调用物种的繁殖方法
                if (offspring) new_individuals.push_back(std::move(offspring)); // 加入新个体列表
                // std::move 将 unique_ptr 的所有权转移给 push_back，避免拷贝，提高效率
                if (offspring) new_individuals.push_back(std::move(offspring)); 
            }
        }
        species_registry.extend_individuals(name, new_individuals);
        SpeciesType type = species_type_from_name(name);
        births.increment(type, new_individuals.size());
        if (!new_individuals.empty()) {
            std::cout << (name == "grass" ? "🌱" : name == "cow" ? "🐄" : "🐅")
                      << " " << new_individuals.size() << " new " << name << " individuals born\n";
        }
    }
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

/*
使用统一逻辑从所有物种中移除死亡个体
*/
void EcosystemState::cleanup_dead() {
    for (const auto& name : species_registry.get_all_species_names()) {
        auto& list = species_registry.get_species_list(name);
        int dead_count = std::count_if(list.begin(), list.end(),
            [](const std::shared_ptr<Species>& s){ return !s->alive; });
        SpeciesType type = species_type_from_name(name);
        deaths.increment(type, dead_count);
        species_registry.filter_alive(name);
        if (dead_count > 0) {
            std::cout << "💀 " << dead_count << " " << name << " individuals died\n";
        }
    }
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