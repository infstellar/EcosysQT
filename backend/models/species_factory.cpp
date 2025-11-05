/*
物种工厂类实现
实现物种创建的工厂模式，支持动态注册和创建物种实例
*/

#include "species_factory.h"
#include "species_params.h"
#include <stdexcept>

// 全局工厂实例定义
SpeciesFactory g_species_factory;

// 注册物种创建函数
void SpeciesFactory::register_species(const std::string& name, Creator creator_func) {
    creators[name] = creator_func;
}

// 根据名称创建物种实例
std::unique_ptr<Species> SpeciesFactory::create(const std::string& name, Position pos) {
    auto it = creators.find(name);
    if (it == creators.end()) {
        throw std::invalid_argument("Unknown species name: " + name);
    }
    return it->second(pos);
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

// 注册所有物种的函数实现
void register_all_species() {
    auto provider = g_species_factory.get_config_provider();
    if (!provider) {
        throw std::runtime_error("Config provider must be set before registering species");
    }

    // 注册草（基于配置参数）
    g_species_factory.register_species("grass", [provider](Position pos) {
        PlantParams params = provider->get_grass_params();
        return std::make_unique<Producer>(pos, params);
    });

    // 注册牛（基于配置参数）
    g_species_factory.register_species("cow", [provider](Position pos) {
        AnimalParams params = provider->get_cow_params();
        auto instance = std::make_unique<Animal>(pos, params);
        instance->species_name = "cow";
        return instance;
    });

    // 注册老虎（基于配置参数）
    g_species_factory.register_species("tiger", [provider](Position pos) {
        AnimalParams params = provider->get_tiger_params();
        auto instance = std::make_unique<Animal>(pos, params);
        instance->species_name = "tiger";
        return instance;
    });
}