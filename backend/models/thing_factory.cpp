/*
Thing factory implementation responsible for plant-like entities (ThingBase derivatives).
*/

#include "thing_factory.h"

#include <stdexcept>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <spdlog/spdlog.h>

#include "producer.h"
#include "species_params.h"
#include "species_config_provider.h"

ThingFactory g_thing_factory;

void ThingFactory::register_species(const std::string& name, Creator creator_func) {
    creators[name] = std::move(creator_func);
}

std::unique_ptr<ThingBase> ThingFactory::create(const std::string& name, Position pos, std::mt19937& rng) {
    const auto it = creators.find(name);
    if (it == creators.end()) {
        throw std::invalid_argument("Unknown thing name: " + name);
    }
    return it->second(pos, rng);
}

std::vector<std::string> ThingFactory::get_all_species_names() const {
    std::vector<std::string> names;
    names.reserve(creators.size());
    for (const auto& pair : creators) {
        names.push_back(pair.first);
    }
    return names;
}

bool ThingFactory::is_registered(const std::string& name) const {
    return creators.find(name) != creators.end();
}

void ThingFactory::clear() {
    creators.clear();
}

static void scan_and_register_plants(const std::string& directory_path) {
    auto provider = g_thing_factory.get_config_provider();
    auto yaml_provider = std::dynamic_pointer_cast<YamlSpeciesConfigProvider>(provider);
    if (!yaml_provider) {
        throw std::runtime_error("Config provider is not YamlSpeciesConfigProvider");
    }

    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Scanning '{}' for plant definitions (FS first)", directory_path);
    QFileInfoList files;
    {
        QDir dir(QString::fromStdString(directory_path));
        if (dir.exists()) {
            dir.setFilter(QDir::Files);
            dir.setNameFilters(QStringList() << "*.yaml");
            files = dir.entryInfoList();
        } else {
            SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[Register] FS directory '{}' missing for plants; will try resource fallback", directory_path);
        }
    }
    if (files.isEmpty()) {
        QDir rdir(QStringLiteral(":/config/species/plants"));
        rdir.setFilter(QDir::Files);
        rdir.setNameFilters(QStringList() << "*.yaml");
        files = rdir.entryInfoList();
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Using resource directory for plants; found {} files", files.size());
    }
    for (const QFileInfo& fi : files) {
        const std::string def_name = fi.baseName().toStdString();

        g_thing_factory.register_species(def_name, [yaml_provider, def_name](Position pos, std::mt19937& rng) {
            PlantParams params = yaml_provider->get_plant_params(def_name);
            auto instance = std::make_unique<Producer>(pos, params, rng);
            instance->species_name = def_name;
            // 如果是草（grass），在创建时为其随机分配一个贴图变体 0..2
            if (def_name == "grass") {
                std::uniform_int_distribution<int> dist(0, 2);
                instance->variant_index = dist(rng);
            } else {
                instance->variant_index = -1;
            }
            return instance;
        });
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Registered plant '{}'", def_name);
    }
}

void register_all_things() {
    auto provider = g_thing_factory.get_config_provider();
    if (!provider) {
        throw std::runtime_error("Config provider must be set before registering things");
    }

    const std::string root = provider->get_config_root_dir();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Config root: '{}'", root);
    scan_and_register_plants(root + "/config/species/plants");

    auto names = g_thing_factory.get_all_species_names();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Total registered things: {}", names.size());
}
