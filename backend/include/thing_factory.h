/*
Thing factory responsible for static or grid-bound entities derived from ThingBase.
*/

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "thing_base.h"
#include "utils.h"
#include "species_config_provider.h"

class ThingFactory {
public:
    using Creator = std::function<std::unique_ptr<ThingBase>(Position pos, std::mt19937& rng)>;

private:
    std::map<std::string, Creator> creators;
    std::shared_ptr<ISpeciesConfigProvider> config_provider;

public:
    void register_species(const std::string& name, Creator creator_func);
    std::unique_ptr<ThingBase> create(const std::string& name, Position pos, std::mt19937& rng);

    void set_config_provider(std::shared_ptr<ISpeciesConfigProvider> provider) { config_provider = std::move(provider); }
    std::shared_ptr<ISpeciesConfigProvider> get_config_provider() const { return config_provider; }

    std::vector<std::string> get_all_species_names() const;
    bool is_registered(const std::string& name) const;
    void clear();
};

extern ThingFactory g_thing_factory;

void register_all_things();
