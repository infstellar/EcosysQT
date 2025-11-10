#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

class RaceBase;
struct EcosystemConfig;

class RacesRegistry {
public:
    struct RaceInfo {
        std::string name;
        std::vector<std::shared_ptr<RaceBase>> list;
        int initial_count;
    };

    explicit RacesRegistry(const EcosystemConfig& config);

    void register_species(const std::string& name, std::shared_ptr<RaceBase> prototype, int initial_count);
    std::vector<std::shared_ptr<RaceBase>>& get_species_list(const std::string& name);
    const std::vector<std::shared_ptr<RaceBase>>& get_species_list(const std::string& name) const;
    int get_initial_count(const std::string& name) const;
    std::vector<std::string> get_all_species_names() const;
    void add_individual(const std::string& name, std::shared_ptr<RaceBase> individual);
    void extend_individuals(const std::string& name, const std::vector<std::shared_ptr<RaceBase>>& individuals);
    void clear_species(const std::string& name);
    void clear_all();
    int get_species_count(const std::string& name) const;
    int get_total_count() const;
    void filter_alive(const std::string& name);
    void filter_all_alive();
    bool has_species(const std::string& name) const;

private:
    std::map<std::string, RaceInfo> registry;
};
