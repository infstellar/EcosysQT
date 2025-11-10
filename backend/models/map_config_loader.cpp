#include "map_config_loader.h"

#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <vector>

static int clamp_int(int v, int lo, int hi, int fallback) {
    if (v < lo || v > hi) return fallback;
    return v;
}

static int read_dimension(const YAML::Node& world_node, const char* key, int fallback) {
    if (!world_node) return fallback;
    const YAML::Node dim_node = world_node[key];
    if (!dim_node) return fallback;
    try {
        return clamp_int(dim_node.as<int>(), 1, 20000, fallback);
    } catch (...) {
        return fallback;
    }
}

EcosystemConfig load_map_config_from_yaml(const std::string& yaml_path) {
    // 默认值，作为回退方案
    EcosystemConfig cfg(1600, 900);

    auto logger = spdlog::get("ecosim");
    if (logger) {
        logger->info("[MapConfig] Loading YAML from '{}'", yaml_path);
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(yaml_path);
    } catch (const std::exception& e) {
        if (logger) logger->warn("[MapConfig] Failed to load '{}': {}. Using defaults.", yaml_path, e.what());
        return cfg;
    }

    // world 尺寸
    if (const auto world = root["world"]; world) {
        const int width = read_dimension(world, "width", cfg.world_width);
        const int height = read_dimension(world, "height", cfg.world_height);
        cfg.world_width = width;
        cfg.world_height = height;
    } else if (logger) {
        logger->warn("[MapConfig] 'world.width/height' missing. Using defaults {}x{}.", cfg.world_width, cfg.world_height);
    }

    // populations
    const auto merge_map = [&](const YAML::Node& node) {
        if (!node || !node.IsMap()) return;
        for (const auto& entry : node) {
            if (!entry.first.IsScalar()) continue;
            try {
                const std::string name = entry.first.as<std::string>();
                const int count = std::max(0, entry.second.as<int>());
                cfg.initial_populations[name] = count;
            } catch (...) {
                // 忽略无法解析项
            }
        }
    };

    std::vector<YAML::Node> population_nodes;
    if (const auto flat = root["initial_populations"]; flat) population_nodes.push_back(flat);
    if (const auto pops = root["populations"]; pops) {
        if (const auto races = pops["races"]; races) population_nodes.push_back(races);
        if (const auto things = pops["things"]; things) population_nodes.push_back(things);
    }

    for (const auto& node : population_nodes) {
        merge_map(node);
    }

    if (cfg.initial_populations.empty()) {
        if (logger) logger->warn("[MapConfig] No populations specified. Using defaults: grass=10, cow=20, tiger=3.");
        cfg.initial_populations["grass"] = 10;
        cfg.initial_populations["cow"] = 20;
        cfg.initial_populations["tiger"] = 3;
    } else if (logger) {
        for (const auto& kv : cfg.initial_populations) {
            logger->info("[MapConfig] init '{}' = {}", kv.first, kv.second);
        }
    }

    return cfg;
}