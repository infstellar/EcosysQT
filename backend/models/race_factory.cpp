/*
Race factory implementation responsible for movable races (animals).
*/

#include "race_factory.h"

#include <stdexcept>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <spdlog/spdlog.h>

#include "animal.h"
#include "species_params.h"
#include "species_config_provider.h"

RaceFactory g_race_factory;

void RaceFactory::register_species(const std::string& name, Creator creator_func) {
    creators[name] = std::move(creator_func);
}

std::unique_ptr<RaceBase> RaceFactory::create(const std::string& name, Position pos, std::mt19937& rng) {
    const auto it = creators.find(name);
    if (it == creators.end()) {
        throw std::invalid_argument("Unknown race name: " + name);
    }
    return it->second(pos, rng);
}

std::vector<std::string> RaceFactory::get_all_species_names() const {
    std::vector<std::string> names;
    names.reserve(creators.size());
    for (const auto& pair : creators) {
        names.push_back(pair.first);
    }
    return names;
}

bool RaceFactory::is_registered(const std::string& name) const {
    return creators.find(name) != creators.end();
}

void RaceFactory::clear() {
    creators.clear();
}

static void scan_and_register_animals(const std::string& directory_path) {
    auto provider = g_race_factory.get_config_provider();
    auto yaml_provider = std::dynamic_pointer_cast<YamlSpeciesConfigProvider>(provider);
    if (!yaml_provider) {
        throw std::runtime_error("Config provider is not YamlSpeciesConfigProvider");
    }

    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Scanning '{}' for animal definitions", directory_path);
    QDir dir(QString::fromStdString(directory_path));
    if (!dir.exists()) {
        SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[Register] Directory '{}' does not exist, skipping animal scan", directory_path);
        return;
    }

    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << "*.yaml");
    const QFileInfoList files = dir.entryInfoList();
    for (const QFileInfo& fi : files) {
        const std::string def_name = fi.baseName().toStdString();

        g_race_factory.register_species(def_name, [yaml_provider, def_name](Position pos, std::mt19937& rng) {
            AnimalParams params = yaml_provider->get_animal_params(def_name);
            // 在构造时传入物种名，确保构造中即可加载 YAML 行为树
            auto instance = std::make_unique<Animal>(pos, def_name, params, rng);
            // 将 YAML 的 bt_params 注入行为树黑板，支持编辑器/配置驱动的时长参数
            instance->apply_bt_params_to_blackboard(params);
            return instance;
        });
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Registered animal '{}'", def_name);
    }
}

void register_all_races() {
    auto provider = g_race_factory.get_config_provider();
    if (!provider) {
        throw std::runtime_error("Config provider must be set before registering races");
    }

    const std::string root = provider->get_config_root_dir();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Config root: '{}'", root);
    scan_and_register_animals(root + "/config/species/animals");

    auto names = g_race_factory.get_all_species_names();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Total registered races: {}", names.size());
}
