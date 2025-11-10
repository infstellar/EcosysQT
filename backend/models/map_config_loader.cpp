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

    // simulation 参数（可选覆盖）
    auto read_sim_param = [&](const YAML::Node& node, const char* key, int fallback, int lo, int hi) -> int {
        if (!node) return fallback;
        const YAML::Node p = node[key];
        if (!p) return fallback;
        try {
            return clamp_int(p.as<int>(), lo, hi, fallback);
        } catch (...) {
            return fallback;
        }
    };

    if (const auto sim = root["simulation"]; sim) {
        const int old_tpd = cfg.ticks_per_day;
        const int old_tph = cfg.ticks_per_hour;
        const int old_max = cfg.max_thing_placement_attempts;
        const int old_dpy = cfg.days_per_year;
        const int old_qpy = cfg.quadrums_per_year;
        const int old_dpq = cfg.days_per_quadrum;

        cfg.ticks_per_day = read_sim_param(sim, "ticks_per_day", cfg.ticks_per_day, 1, 1000000);
        cfg.ticks_per_hour = read_sim_param(sim, "ticks_per_hour", cfg.ticks_per_hour, 1, 1000000);
        cfg.max_thing_placement_attempts = read_sim_param(sim, "max_thing_placement_attempts", cfg.max_thing_placement_attempts, 1, 100000);

        // 约束校验：确保 hour 是 day 的因子（映射到 24 小时）
        if (cfg.ticks_per_day % cfg.ticks_per_hour != 0) {
            if (logger) {
                logger->warn("[MapConfig] Invalid simulation: ticks_per_day % ticks_per_hour != 0 ({} % {}), falling back to defaults {}:{}.",
                             cfg.ticks_per_day, cfg.ticks_per_hour, old_tpd, old_tph);
            }
            cfg.ticks_per_day = old_tpd;
            cfg.ticks_per_hour = old_tph;
        }
        if (cfg.max_thing_placement_attempts <= 0) {
            if (logger) {
                logger->warn("[MapConfig] Invalid simulation: max_thing_placement_attempts <= 0 ({}), falling back to {}.",
                             cfg.max_thing_placement_attempts, old_max);
            }
            cfg.max_thing_placement_attempts = old_max;
        }

        // 读取年/季度相关参数（可选）。
        bool provided_dpy = false;
        bool provided_qpy = false;
        bool provided_dpq = false;
        int dpy = cfg.days_per_year;
        int qpy = cfg.quadrums_per_year;
        int dpq = cfg.days_per_quadrum;

        if (sim["days_per_year"]) {
            try { dpy = clamp_int(sim["days_per_year"].as<int>(), 1, 100000, dpy); provided_dpy = true; } catch (...) {}
        }
        if (sim["quadrums_per_year"]) {
            try { qpy = clamp_int(sim["quadrums_per_year"].as<int>(), 1, 24, qpy); provided_qpy = true; } catch (...) {}
        }
        if (sim["days_per_quadrum"]) {
            try { dpq = clamp_int(sim["days_per_quadrum"].as<int>(), 1, 10000, dpq); provided_dpq = true; } catch (...) {}
        }

        // 规范化三元组关系：days_per_year == quadrums_per_year * days_per_quadrum
        if (provided_dpy || provided_qpy || provided_dpq) {
            if (provided_dpy && provided_qpy && !provided_dpq) {
                if (dpy % qpy == 0) {
                    dpq = dpy / qpy;
                } else {
                    if (logger) {
                        logger->warn("[MapConfig] Invalid year/quadrum: days_per_year % quadrums_per_year != 0 ({} % {}), falling back to defaults {}={}*{}.", dpy, qpy, old_dpy, old_qpy, old_dpq);
                    }
                    dpy = old_dpy; qpy = old_qpy; dpq = old_dpq;
                }
            } else if (provided_dpy && provided_dpq && !provided_qpy) {
                if (dpy % dpq == 0) {
                    qpy = dpy / dpq;
                } else {
                    if (logger) {
                        logger->warn("[MapConfig] Invalid year/quadrum: days_per_year % days_per_quadrum != 0 ({} % {}), falling back to defaults {}={}*{}.", dpy, dpq, old_dpy, old_qpy, old_dpq);
                    }
                    dpy = old_dpy; qpy = old_qpy; dpq = old_dpq;
                }
            } else if (!provided_dpy && provided_qpy && provided_dpq) {
                dpy = qpy * dpq;
            } else if (provided_dpy && !provided_qpy && !provided_dpq) {
                // 仅提供了天数：尝试以现有季数求每季天数
                if (dpy % qpy == 0) {
                    dpq = dpy / qpy;
                } else {
                    if (logger) {
                        logger->warn("[MapConfig] days_per_year ({}) incompatible with quadrums_per_year ({}). Keeping quadrums_per_year={}, days_per_quadrum={}, and deriving days_per_year.", dpy, qpy, old_qpy, old_dpq);
                    }
                    qpy = old_qpy; dpq = old_dpq; dpy = qpy * dpq;
                }
            } else if (!provided_dpy && provided_qpy && !provided_dpq) {
                dpy = qpy * dpq;
            } else if (!provided_dpy && !provided_qpy && provided_dpq) {
                dpy = qpy * dpq;
            } else { // 全部提供或其他组合：优先保证一致性
                if (dpy != qpy * dpq) {
                    if (logger) {
                        logger->warn("[MapConfig] Inconsistent year/quadrum triple: {} != {} * {}. Using product.", dpy, qpy, dpq);
                    }
                    dpy = qpy * dpq;
                }
            }

            cfg.days_per_year = dpy;
            cfg.quadrums_per_year = qpy;
            cfg.days_per_quadrum = dpq;
        }

        if (logger) {
            logger->info("[MapConfig] simulation.ticks_per_day = {}", cfg.ticks_per_day);
            logger->info("[MapConfig] simulation.ticks_per_hour = {}", cfg.ticks_per_hour);
            logger->info("[MapConfig] simulation.max_thing_placement_attempts = {}", cfg.max_thing_placement_attempts);
            logger->info("[MapConfig] simulation.days_per_year = {}", cfg.days_per_year);
            logger->info("[MapConfig] simulation.quadrums_per_year = {}", cfg.quadrums_per_year);
            logger->info("[MapConfig] simulation.days_per_quadrum = {}", cfg.days_per_quadrum);
        }
    } else if (logger) {
        logger->debug("[MapConfig] 'simulation' node missing. Using defaults for ticks/hour/placement/year/quadrum.");
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