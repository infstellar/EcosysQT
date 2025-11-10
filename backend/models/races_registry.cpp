#include "races_registry.h"

#include <algorithm>
#include <utility>

#include "ecosystem.h"
#include "race_factory.h"
#include "race_base.h"

RacesRegistry::RacesRegistry(const EcosystemConfig& config) {
    // 从工厂获取所有（已自动扫描注册的）物种名
    for (const auto& name : g_race_factory.get_all_species_names()) {
        int initial_count = 0;
        // 从新的 config 结构中查找初始数量
        auto it = config.initial_populations.find(name);
        if (it != config.initial_populations.end()) {
            initial_count = it->second;
        }

        // 注册物种条目，原型 `prototype` 未使用，传入 nullptr
        register_species(name, nullptr, initial_count);
    }
}

void RacesRegistry::register_species(const std::string& name, std::shared_ptr<RaceBase> /*prototype*/, int initial_count) {
    registry[name] = RaceInfo{name, {}, initial_count};
}

std::vector<std::shared_ptr<RaceBase>>& RacesRegistry::get_species_list(const std::string& name) {
    return registry[name].list;
}

const std::vector<std::shared_ptr<RaceBase>>& RacesRegistry::get_species_list(const std::string& name) const {
    return registry.at(name).list;
}

int RacesRegistry::get_initial_count(const std::string& name) const {
    auto it = registry.find(name);
    return it != registry.end() ? it->second.initial_count : 0;
}

std::vector<std::string> RacesRegistry::get_all_species_names() const {
    std::vector<std::string> names;
    for (const auto& kv : registry) {
        names.push_back(kv.first);
    }
    return names;
}

void RacesRegistry::add_individual(const std::string& name, std::shared_ptr<RaceBase> individual) {
    registry[name].list.push_back(std::move(individual));
}

void RacesRegistry::extend_individuals(const std::string& name, const std::vector<std::shared_ptr<RaceBase>>& individuals) {
    auto& list = registry[name].list;
    list.insert(list.end(), individuals.begin(), individuals.end());
}

void RacesRegistry::clear_species(const std::string& name) {
    registry[name].list.clear();
}

void RacesRegistry::clear_all() {
    for (auto& kv : registry) {
        kv.second.list.clear();
    }
}

int RacesRegistry::get_species_count(const std::string& name) const {
    auto it = registry.find(name);
    return it != registry.end() ? static_cast<int>(it->second.list.size()) : 0;
}

int RacesRegistry::get_total_count() const {
    int sum = 0;
    for (const auto& kv : registry) {
        sum += static_cast<int>(kv.second.list.size());
    }
    return sum;
}

void RacesRegistry::filter_alive(const std::string& name) {
    auto& list = registry[name].list;
    list.erase(std::remove_if(list.begin(), list.end(), [](const std::shared_ptr<RaceBase>& race) {
        return !race || !race->alive;
    }), list.end());
}

void RacesRegistry::filter_all_alive() {
    for (auto& kv : registry) {
        filter_alive(kv.first);
    }
}

bool RacesRegistry::has_species(const std::string& name) const {
    return registry.find(name) != registry.end();
}
