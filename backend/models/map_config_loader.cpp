#include "map_config_loader.h"

#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <QFile>
#include <QIODevice>
#include <QString>

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
        // 文件系统优先
        root = YAML::LoadFile(yaml_path);
        if (logger) logger->info("[MapConfig] Loaded from FS: '{}'", yaml_path);
    } catch (const std::exception& e) {
        if (logger) logger->warn("[MapConfig] FS load failed for '{}': {}. Trying resource.", yaml_path, e.what());
        // 资源兜底：固定别名路径
        const QString qrcPath = QStringLiteral(":/config/map_config.yaml");
        QFile f(qrcPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray content = f.readAll();
            f.close();
            try {
                root = YAML::Load(std::string(content.constData(), static_cast<size_t>(content.size())));
                if (logger) logger->info("[MapConfig] Loaded from resource: '{}'", qrcPath.toStdString());
            } catch (const std::exception& e2) {
                if (logger) logger->warn("[MapConfig] Resource load failed for '{}': {}. Using defaults.", qrcPath.toStdString(), e2.what());
                return cfg;
            }
        } else {
            if (logger) logger->warn("[MapConfig] Resource not available: '{}'. Using defaults.", qrcPath.toStdString());
            return cfg;
        }
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

    if (const auto map_gen = root["map_generation"]; map_gen) {
        if (logger) logger->info("[MapConfig] Loading 'map_generation' parameters...");
        auto& mg_cfg = cfg.map_gen_config;

        const auto read_float = [&](const char* key, float fallback) -> float {
            if (const auto node = map_gen[key]; node) {
                try {
                    return node.as<float>();
                } catch (...) {
                    if (logger) logger->warn("[MapConfig] Invalid float for map_generation.{}, using fallback {}", key, fallback);
                }
            }
            return fallback;
        };

        const auto read_double = [&](const char* key, double fallback) -> double {
            if (const auto node = map_gen[key]; node) {
                try {
                    return node.as<double>();
                } catch (...) {
                    if (logger) logger->warn("[MapConfig] Invalid double for map_generation.{}, using fallback {}", key, fallback);
                }
            }
            return fallback;
        };

        mg_cfg.elevation_frequency = read_float("elevation_frequency", mg_cfg.elevation_frequency);
        mg_cfg.moisture_frequency = read_float("moisture_frequency", mg_cfg.moisture_frequency);
        mg_cfg.river_frequency = read_float("river_frequency", mg_cfg.river_frequency);
        mg_cfg.river_threshold = read_float("river_threshold", mg_cfg.river_threshold);
        mg_cfg.tiles_per_degree = read_double("tiles_per_degree", mg_cfg.tiles_per_degree);
        mg_cfg.base_latitude = read_double("base_latitude", mg_cfg.base_latitude);
        mg_cfg.base_longitude = read_double("base_longitude", mg_cfg.base_longitude);

        if (logger) {
            logger->info("[MapConfig]   elev_freq: {}", mg_cfg.elevation_frequency);
            logger->info("[MapConfig]   moisture_freq: {}", mg_cfg.moisture_frequency);
            logger->info("[MapConfig]   river_freq: {}", mg_cfg.river_frequency);
            logger->info("[MapConfig]   river_threshold: {}", mg_cfg.river_threshold);
            logger->info("[MapConfig]   tiles_per_degree: {}", mg_cfg.tiles_per_degree);
            if (std::abs(mg_cfg.base_latitude - 999.0) < 1e-6) {
                logger->info("[MapConfig]   base_lat: Random");
            } else {
                logger->info("[MapConfig]   base_lat: {}", mg_cfg.base_latitude);
            }
            if (std::abs(mg_cfg.base_longitude - 999.0) < 1e-6) {
                logger->info("[MapConfig]   base_lon: Random");
            } else {
                logger->info("[MapConfig]   base_lon: {}", mg_cfg.base_longitude);
            }
        }
    } else if (logger) {
        logger->info("[MapConfig] 'map_generation' node missing. Using default map parameters.");
    }

    return cfg;
}