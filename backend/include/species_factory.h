/*
物种工厂类
实现物种创建的工厂模式，支持动态注册和创建物种实例
*/

#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <vector>
#include "species.h"
#include "utils.h"
#include "species_config_provider.h"

class SpeciesFactory {
public:
    // 物种创建函数类型定义
    using Creator = std::function<std::unique_ptr<Species>(Position)>;

private:
    // 从物种名称到创建函数的映射
    std::map<std::string, Creator> creators;
    // 配置提供者（用于构造物种所需参数）
    std::shared_ptr<ISpeciesConfigProvider> config_provider;

public:
    // 注册物种创建函数
    void register_species(const std::string& name, Creator creator_func);
    
    // 根据名称创建物种实例
    std::unique_ptr<Species> create(const std::string& name, Position pos);
    
    // 注入配置提供者
    void set_config_provider(std::shared_ptr<ISpeciesConfigProvider> provider) { config_provider = std::move(provider); }
    std::shared_ptr<ISpeciesConfigProvider> get_config_provider() const { return config_provider; }
    
    // 获取所有已注册物种的名称
    std::vector<std::string> get_all_species_names() const;
    
    // 检查物种是否已注册
    bool is_registered(const std::string& name) const;
    
    // 清除所有注册的物种
    void clear();
};

// 全局工厂实例声明
extern SpeciesFactory g_species_factory;

// 注册所有物种的函数声明
void register_all_species();