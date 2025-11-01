// 物种配置提供者接口与YAML实现声明
#pragma once

#include <memory>
#include <string>
#include "species_params.h"

// 抽象配置提供者：按物种返回强类型参数
class ISpeciesConfigProvider {
public:
    virtual ~ISpeciesConfigProvider() = default;
    virtual TigerParams get_tiger_params() const = 0;
    virtual CowParams get_cow_params() const = 0;
    virtual GrassParams get_grass_params() const = 0;
};

// 基于 YAML 的配置提供者（定义在 cpp）
class YamlSpeciesConfigProvider : public ISpeciesConfigProvider {
public:
    explicit YamlSpeciesConfigProvider(std::string config_root_dir);

    TigerParams get_tiger_params() const override;
    CowParams get_cow_params() const override;
    GrassParams get_grass_params() const override;

private:
    std::string root_dir;
};