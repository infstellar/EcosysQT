#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

class Species;
struct EcosystemConfig;

class SpeciesRegistry {
public:
    struct SpeciesInfo {
        std::string name;
        std::vector<std::shared_ptr<Species>> list;
        int initial_count;
    };

    explicit SpeciesRegistry(const EcosystemConfig& config);

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
    bool has_species(const std::string& name) const;

private:
    std::map<std::string, SpeciesInfo> registry;
};
