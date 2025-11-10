#include "map_config_loader.h"

#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <algorithm>

static int clamp_int(int v, int lo, int hi, int fallback) {
    if (v < lo || v > hi) return fallback;
    return v;
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
    try {
        const auto world = root["world"];
        if (world && world["width"] && world["height"]) {
            int w = world["width"].as<int>();
            int h = world["height"].as<int>();
            // 基本边界：避免过大导致内存暴涨；允许用户自由调整，但给出上限
            w = clamp_int(w, 1, 20000, 1600);
            h = clamp_int(h, 1, 20000, 900);
            cfg.world_width = w;
            cfg.world_height = h;
        } else if (logger) {
            logger->warn("[MapConfig] 'world.width/height' missing. Using defaults {}x{}.", cfg.world_width, cfg.world_height);
        }
    } catch (...) {
        if (logger) logger->warn("[MapConfig] Invalid 'world' fields. Using defaults {}x{}.", cfg.world_width, cfg.world_height);
    }

    //  populations ：
    auto merge_map = [&](const YAML::Node& node) {
        if (!node || !node.IsMap()) return;
        for (auto it : node) {
            try {
                const std::string name = it.first.as<std::string>();
                const int count = std::max(0, it.second.as<int>());
                cfg.initial_populations[name] = count;
            } catch (...) {
                // 忽略无法解析项
            }
        }
    };

    bool any_population = false;
    try {
        const auto flat = root["initial_populations"];
        if (flat) {
            merge_map(flat);
            any_population = true;
        }
    } catch (...) {}

    try {
        const auto pops = root["populations"];
        if (pops) {
            merge_map(pops["races"]);
            merge_map(pops["things"]);
            any_population = true;
        }
    } catch (...) {}

    if (!any_population) {
        if (logger) logger->warn("[MapConfig] No populations specified. Using defaults: grass=10, cow=20, tiger=3.");
        cfg.initial_populations["grass"] = 10;
        cfg.initial_populations["cow"] = 20;
        cfg.initial_populations["tiger"] = 3;
    } else if (logger) {
        // 简要打印
        for (const auto& kv : cfg.initial_populations) {
            logger->info("[MapConfig] init '{}' = {}", kv.first, kv.second);
        }
    }

    return cfg;
}