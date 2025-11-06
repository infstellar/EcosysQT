/*
物种工厂类实现
实现物种创建的工厂模式，支持动态注册和创建物种实例
*/

#include "species_factory.h"
#include "species_params.h"
#include "species.h" // 基类 Species 定义
#include "animal.h"  // Animal 定义
#include "producer.h" // Producer 定义
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QString>

// 全局工厂实例定义
SpeciesFactory g_species_factory;

// 注册物种创建函数
void SpeciesFactory::register_species(const std::string& name, Creator creator_func) {
    creators[name] = creator_func;
}

// 根据名称创建物种实例
std::unique_ptr<Species> SpeciesFactory::create(const std::string& name, Position pos, std::mt19937& rng) {
    auto it = creators.find(name);
    if (it == creators.end()) {
        throw std::invalid_argument("Unknown species name: " + name);
    }
    // 将 rng 传递给注册的 lambda 函数
    return it->second(pos, rng);
}

// 获取所有已注册物种的名称
std::vector<std::string> SpeciesFactory::get_all_species_names() const {
    std::vector<std::string> names;
    for (const auto& pair : creators) {
        names.push_back(pair.first);
    }
    return names;
}

// 检查物种是否已注册
bool SpeciesFactory::is_registered(const std::string& name) const {
    return creators.find(name) != creators.end();
}

// 清除所有注册的物种
void SpeciesFactory::clear() {
    creators.clear();
}

// 扫描目录并注册物种的辅助函数 (使用 Qt 重写)
static void scan_and_register(const std::string& directory_path, const std::string& type) {
    auto provider = g_species_factory.get_config_provider();
    // 确保 provider 是 YamlSpeciesConfigProvider
    auto yaml_provider = std::dynamic_pointer_cast<YamlSpeciesConfigProvider>(provider);
    if (!yaml_provider) {
        throw std::runtime_error("Config provider is not YamlSpeciesConfigProvider");
    }

    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Scanning '{}' for {} definitions", directory_path, type);
    QDir dir(QString::fromStdString(directory_path));
    if (!dir.exists()) {
        // 目录不存在则直接返回，允许缺省目录
        SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[Register] Directory '{}' does not exist, skipping {} scan", directory_path, type);
        return;
    }

    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << "*.yaml");
    const QFileInfoList files = dir.entryInfoList();
    for (const QFileInfo& fi : files) {
        const std::string defName = fi.baseName().toStdString();

        if (type == "Animal") {
            // 修改 lambda，使其接收 rng 并传递给 Animal 构造函数
            g_species_factory.register_species(defName, [yaml_provider, defName](Position pos, std::mt19937& rng) {
                AnimalParams params = yaml_provider->get_animal_params(defName);
                auto instance = std::make_unique<Animal>(pos, params, rng); // 传递 rng
                instance->species_name = defName;
                return instance;
            });
            SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Registered animal '{}'", defName);
        } else if (type == "Plant") {
            // 植物的构造函数不需要 rng，所以 lambda 可以忽略它
            g_species_factory.register_species(defName, [yaml_provider, defName](Position pos, std::mt19937& /*rng*/) {
                PlantParams params = yaml_provider->get_plant_params(defName);
                auto instance = std::make_unique<Producer>(pos, params);
                instance->species_name = defName;
                return instance;
            });
            SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Registered plant '{}'", defName);
        }
    }
}

// 注册所有物种的函数实现（自动扫描 animals / plants 目录）
void register_all_species() {
    auto provider = g_species_factory.get_config_provider();
    if (!provider) {
        throw std::runtime_error("Config provider must be set before registering species");
    }

    const std::string root = provider->get_config_root_dir();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Config root: '{}'", root);
    // 自动扫描并注册所有动物
    scan_and_register(root + "/config/species/animals", "Animal");
    // 自动扫描并注册所有植物
    scan_and_register(root + "/config/species/plants", "Plant");

    auto names = g_species_factory.get_all_species_names();
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Register] Total registered species: {}", names.size());
}