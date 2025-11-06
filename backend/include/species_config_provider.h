// 物种配置提供者接口与YAML实现声明
#pragma once

#include <memory>
#include <string>
#include "species_params.h"

// 抽象配置提供者：按物种返回强类型参数
class ISpeciesConfigProvider {
public:
    virtual ~ISpeciesConfigProvider() = default;
    virtual AnimalParams get_animal_params(const std::string& name) const = 0;
    virtual PlantParams get_plant_params(const std::string& name) const = 0;
    // 返回配置根目录，用于自动扫描注册
    virtual std::string get_config_root_dir() const = 0;
};

// 基于 YAML 的配置提供者（定义在 cpp）
class YamlSpeciesConfigProvider : public ISpeciesConfigProvider {
public:
    explicit YamlSpeciesConfigProvider(std::string config_root_dir);

    AnimalParams get_animal_params(const std::string& name) const override;
    PlantParams get_plant_params(const std::string& name) const override;
    std::string get_config_root_dir() const override;

private:
    std::string root_dir;
};