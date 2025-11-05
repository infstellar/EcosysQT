#include "species_registry.h"

#include <algorithm>
#include <utility>

#include "ecosystem.h"
#include "species_factory.h"
#include "species.h"

SpeciesRegistry::SpeciesRegistry(const EcosystemConfig& config) {
    {
        auto proto_unique = g_species_factory.create("grass", Position{0, 0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("grass", proto, config.initial_grass);
    }
    {
        auto proto_unique = g_species_factory.create("cow", Position{0, 0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("cow", proto, config.initial_cows);
    }
    {
        auto proto_unique = g_species_factory.create("tiger", Position{0, 0});
        std::shared_ptr<Species> proto = std::move(proto_unique);
        register_species("tiger", proto, config.initial_tigers);
    }
}

void SpeciesRegistry::register_species(const std::string& name, std::shared_ptr<Species> /*prototype*/, int initial_count) {
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
    for (const auto& kv : registry) {
        names.push_back(kv.first);
    }
    return names;
}

void SpeciesRegistry::add_individual(const std::string& name, std::shared_ptr<Species> individual) {
    registry[name].list.push_back(std::move(individual));
}

void SpeciesRegistry::extend_individuals(const std::string& name, const std::vector<std::shared_ptr<Species>>& individuals) {
    auto& list = registry[name].list;
    list.insert(list.end(), individuals.begin(), individuals.end());
}

void SpeciesRegistry::clear_species(const std::string& name) {
    registry[name].list.clear();
}

void SpeciesRegistry::clear_all() {
    for (auto& kv : registry) {
        kv.second.list.clear();
    }
}

int SpeciesRegistry::get_species_count(const std::string& name) const {
    auto it = registry.find(name);
    return it != registry.end() ? static_cast<int>(it->second.list.size()) : 0;
}

int SpeciesRegistry::get_total_count() const {
    int sum = 0;
    for (const auto& kv : registry) {
        sum += static_cast<int>(kv.second.list.size());
    }
    return sum;
}

void SpeciesRegistry::filter_alive(const std::string& name) {
    auto& list = registry[name].list;
    list.erase(std::remove_if(list.begin(), list.end(), [](const std::shared_ptr<Species>& s) {
        return !s || !s->alive;
    }), list.end());
}

void SpeciesRegistry::filter_all_alive() {
    for (auto& kv : registry) {
        filter_alive(kv.first);
    }
}

bool SpeciesRegistry::has_species(const std::string& name) const {
    return registry.find(name) != registry.end();
}
