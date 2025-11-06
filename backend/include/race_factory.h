/*
Race factory responsible for creating movable entities derived from RaceBase.
*/

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "race_base.h"
#include "utils.h"
#include "species_config_provider.h"

class RaceFactory {
public:
    using Creator = std::function<std::unique_ptr<RaceBase>(Position pos, std::mt19937& rng)>;

private:
    std::map<std::string, Creator> creators;
    std::shared_ptr<ISpeciesConfigProvider> config_provider;

public:
    void register_species(const std::string& name, Creator creator_func);
    std::unique_ptr<RaceBase> create(const std::string& name, Position pos, std::mt19937& rng);

    void set_config_provider(std::shared_ptr<ISpeciesConfigProvider> provider) { config_provider = std::move(provider); }
    std::shared_ptr<ISpeciesConfigProvider> get_config_provider() const { return config_provider; }

    std::vector<std::string> get_all_species_names() const;
    bool is_registered(const std::string& name) const;
    void clear();
};

extern RaceFactory g_race_factory;

void register_all_races();
