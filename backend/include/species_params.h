// 强类型物种参数结构体定义
#pragma once

#include <string>
#include <vector>
// 反射支持：用于自动从 YAML 键匹配到成员名
#include <boost/describe.hpp>

// 基础物种参数
struct SpeciesBaseParams {
    double energy = 100.0;
    int max_age = 100;
    double reproduction_energy_cost = 50.0;
};

// 动物通用参数（继承基础物种）
struct AnimalParams : SpeciesBaseParams {
    double movement_speed = 1.0;
    int energy_consumption = 1;
    double hunting_range = 5.0;
    double hunting_success_rate = 0.5;
    double detection_range = 500.0;
    std::vector<std::string> food_types = {};
    int hunting_cooldown_duration = 0;
    int min_reproduction_age = 0;
    int reproduction_cooldown = 0;
    double eating_range = 0.0;
    double satisfied_threshold_ratio = 0.8;   // 吃饱阈值比例
    double starving_threshold_ratio = 0.2;    // 饥饿阈值比例
    int wandering_duration = 50;              // 逛街持续时间
};

// 老虎特有参数（继承动物参数）
struct TigerParams : AnimalParams {
};

// 牛特有参数（继承动物参数）
struct CowParams : AnimalParams {
};

// 草参数（继承基础物种）
struct GrassParams : SpeciesBaseParams {
    double base_growth_rate = 0.9;
    double reproduction_chance = 0.4;
    double competition_radius = 30.0;
    double max_competition_effect = 0.9;
    int reproduction_cooldown = 10;
};

// 为自动匹配提供成员名与继承关系描述（一次性声明，保持 DRY）
BOOST_DESCRIBE_STRUCT(SpeciesBaseParams, (),
    (energy, max_age, reproduction_energy_cost))
BOOST_DESCRIBE_STRUCT(AnimalParams, (SpeciesBaseParams),
    (movement_speed, energy_consumption, hunting_range, hunting_success_rate, detection_range, food_types, hunting_cooldown_duration, min_reproduction_age, reproduction_cooldown, eating_range, satisfied_threshold_ratio, starving_threshold_ratio, wandering_duration))
BOOST_DESCRIBE_STRUCT(TigerParams, (AnimalParams),
    ())
BOOST_DESCRIBE_STRUCT(CowParams, (AnimalParams),
    ())
BOOST_DESCRIBE_STRUCT(GrassParams, (SpeciesBaseParams),
    (base_growth_rate, reproduction_chance, competition_radius, max_competition_effect, reproduction_cooldown))