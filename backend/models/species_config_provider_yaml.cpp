/*
YAML 物种配置提供者实现
从 config/species/*.yaml 加载并解析参数，提供强类型结构体
*/

#include "species_config_provider.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <spdlog/spdlog.h>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <QFile>
#include <QIODevice>
// 反射: 成员名与继承枚举
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <type_traits>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>
#include <limits>

namespace {

std::string normalize_terrain_key(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char ch : raw) {
        if (ch == '-' || ch == ' ' || ch == '.') {
            out.push_back('_');
        } else {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

std::optional<TerrainType> terrain_type_from_string(const std::string& raw) {
    const std::string key = normalize_terrain_key(raw);
    if (key == "LAND") return TerrainType::LAND;
    if (key == "WATER") return TerrainType::WATER;
    if (key == "SHALLOW_RIVER") return TerrainType::SHALLOW_RIVER;
    if (key == "DEEP_RIVER") return TerrainType::DEEP_RIVER;
    if (key == "SHALLOW_OCEAN") return TerrainType::SHALLOW_OCEAN;
    if (key == "DEEP_OCEAN") return TerrainType::DEEP_OCEAN;
    if (key == "SAND") return TerrainType::SAND;
    if (key == "INLAND_SAND") return TerrainType::INLAND_SAND;
    if (key == "HILLS") return TerrainType::HILLS;
    if (key == "MOUNTAIN") return TerrainType::MOUNTAIN;
    return std::nullopt;
}

void parse_pathfinding_params_node(const YAML::Node& node, PathfindingParams& params) {
    if (!node) return;
    const YAML::Node cfg = node["pathfinding"];
    if (!cfg) return;

    if (cfg["replan_interval"]) {
        try {
            params.replan_interval = std::max(0, cfg["replan_interval"].as<int>());
        } catch (...) {
        }
    }

    if (cfg["enable_smoothing"]) {
        try {
            params.enable_smoothing = cfg["enable_smoothing"].as<bool>();
        } catch (...) {
        }
    }

    if (cfg["budget_multiplier"]) {
        try {
            const double v = cfg["budget_multiplier"].as<double>();
            if (std::isfinite(v) && v >= 0.0) {
                params.budget_multiplier = v;
            }
        } catch (...) {
        }
    }

    if (cfg["terrain_costs"] && cfg["terrain_costs"].IsMap()) {
        for (auto it : cfg["terrain_costs"]) {
            std::optional<TerrainType> terrain;
            try {
                terrain = terrain_type_from_string(it.first.as<std::string>());
            } catch (...) {
                terrain.reset();
            }
            if (!terrain.has_value()) {
                continue;
            }

            try {
                const double cost = it.second.as<double>();
                if (!std::isfinite(cost) || cost <= 0.0) {
                    params.terrain_cost_overrides[*terrain] = std::numeric_limits<double>::infinity();
                } else {
                    params.terrain_cost_overrides[*terrain] = cost;
                }
            } catch (...) {
                // 忽略不可转换的值
            }
        }
    }
}

} // namespace

// 使用简单的字符串拼接来处理路径，避免 GCC 8 对 std::filesystem 的兼容性问题

// 兼容 MinGW(GCC 8) 在 Windows 下的宽/窄字符路径问题：
// 使用 u8path 构造路径，并用 u8string 传给第三方库（如 yaml-cpp）。
static YAML::Node load_yaml_file(const std::string& p) {
    try {
        return YAML::LoadFile(p);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] Failed to load YAML: {}, error: {}", p, e.what());
        return YAML::Node();
    }
}

// 辅助函数：按顺序查找 YAML 文件 (使用 Qt 重写)
// 广泛搜索：在根目录下的 config 目录中递归查找 name.yaml
/* TODO: 
后续优化
- 缓存“文件名 → 绝对路径”的搜索结果，避免多次递归搜索带来的性能开销（尤其配置体量增大时）。
- 限定广泛搜索的根为 config/species 与 config/species_base ，既满足你的“广泛查找”，也避免扫描无关目录。
- 为同名文件冲突（不同目录同时有 <name>.yaml ）加入明确优先级策略日志，便于调试歧义。
*/
static std::string search_yaml_path(const std::string& name, const std::string& root_dir, const char* preferred_subfolder) {
    const QString search_root = QString::fromStdString(root_dir) + "/config";
    std::string first_match;
    std::string preferred_match;

    QDirIterator it(search_root, QStringList() << "*.yaml", QDir::Files, QDirIterator::Subdirectories);
    const QString qname = QString::fromStdString(name);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fi(filePath);
        if (fi.baseName() == qname) {
            const std::string full = filePath.toStdString();
            if (first_match.empty()) first_match = full;
            if (preferred_subfolder) {
                const QString pref = QString::fromLatin1(preferred_subfolder);
                if (filePath.contains("/" + pref + "/") || filePath.contains("\\" + pref + "\\")) {
                    preferred_match = full;
                }
            }
        }
    }
    return !preferred_match.empty() ? preferred_match : first_match;
}

// 计算主优先路径：
// - 普通物种名：config/species/<category>/<name>.yaml
// - 基础模板：name 以 "base_" 开头 -> config/species_base/<name>.yaml
// - 通用基类：name == "species" -> config/species.yaml
static std::string resolve_primary_path(const std::string& name, const std::string& root_dir, const char* category) {
    if (name == "species") {
        return root_dir + std::string("/config/species.yaml");
    }
    if (name.rfind("base_", 0) == 0) {
        return root_dir + std::string("/config/species_base/") + name + ".yaml";
    }
    return root_dir + std::string("/config/species/") + category + "/" + name + ".yaml";
}

// 资源别名路径：与上面保持一致，指向 ":/config/..."
static QString resolve_resource_alias_path(const std::string& name, const char* category) {
    if (name == "species") {
        return QStringLiteral(":/config/species.yaml");
    }
    if (name.rfind("base_", 0) == 0) {
        return QString::fromLatin1(":/config/species_base/") + QString::fromStdString(name) + QStringLiteral(".yaml");
    }
    return QString::fromLatin1(":/config/species/") + QString::fromLatin1(category) + QStringLiteral("/") + QString::fromStdString(name) + QStringLiteral(".yaml");
}

static YAML::Node load_yaml_from_resource(const QString& alias_path) {
    QFile f(alias_path);
    if (!f.open(QIODevice::ReadOnly)) {
        return YAML::Node();
    }
    const QByteArray content = f.readAll();
    f.close();
    try {
        return YAML::Load(std::string(content.constData(), static_cast<size_t>(content.size())));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] Failed to load YAML from resource '{}': {}", alias_path.toStdString(), e.what());
        return YAML::Node();
    }
}

static YAML::Node load_yaml_in_category(const std::string& name, const std::string& root_dir, const char* category) {
    const std::string primary = resolve_primary_path(name, root_dir, category);
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Loading '{}' YAML for '{}' from '{}'", category, name, primary);
    // 文件系统优先
    try {
        auto node = YAML::LoadFile(primary);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Loaded '{}' from FS: '{}'", name, primary);
        return node;
    } catch (const std::exception& e) {
        // 资源别名兜底
        const QString alias = resolve_resource_alias_path(name, category);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] FS missing for '{}', try resource '{}'", name, alias.toStdString());
        YAML::Node node_resource = load_yaml_from_resource(alias);
        if (node_resource) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Loaded '{}' from resource: '{}'", name, alias.toStdString());
            return node_resource;
        }
        // 主路径缺失属于正常回退场景，降低为 DEBUG，避免污染 info 日志
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Primary '{}' YAML not available for '{}': {}. Searching...", category, name, e.what());
        const std::string found = search_yaml_path(name, root_dir, category);
        if (!found.empty()) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Found '{}' via search at '{}'", name, found);
            try {
                return YAML::LoadFile(found);
            } catch (const std::exception& e2) {
                SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] Failed to load searched '{}' YAML for '{}': {}", category, name, e2.what());
                throw std::runtime_error("Failed to load YAML '" + name + "' at '" + primary + "', resource '" + alias.toStdString() + "' and searched '" + found + "': " + e2.what());
            }
        }
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] '{}' YAML for '{}' not found after FS, resource, and search", category, name);
        throw std::runtime_error("Failed to locate YAML '" + name + "' from FS '" + primary + "', resource alias '" + alias.toStdString() + "' or anywhere under '" + (root_dir + "/config") + "'");
    }
}

static YAML::Node load_animal_yaml(const std::string& name, const std::string& root_dir) {
    return load_yaml_in_category(name, root_dir, "animals");
}

static YAML::Node load_plant_yaml(const std::string& name, const std::string& root_dir) {
    return load_yaml_in_category(name, root_dir, "plants");
}

// 通用：按成员名自动赋值（支持继承成员），避免映射表
template <class T>
static void apply_yaml_fields_by_name(const YAML::Node& node, T& params) {
    using namespace boost::describe;
    using Members = describe_members<T, mod_any_access | mod_inherited>;
    boost::mp11::mp_for_each<Members>([&](auto const& D) {
        if (!node) return;
        const char* name = D.name; // 成员名
        if (!name || !node[name]) return;
        auto ptr = D.pointer;      // 成员指针
        using MemberRef = decltype(params.*ptr);
        using Member = std::remove_reference_t<MemberRef>;
        try {
            if constexpr (std::is_same_v<Member, int>) {
                (params.*ptr) = node[name].as<int>();
            } else if constexpr (std::is_same_v<Member, bool>) {
                (params.*ptr) = node[name].as<bool>();
            } else if constexpr (std::is_same_v<Member, double>) {
                (params.*ptr) = node[name].as<double>();
            } else if constexpr (std::is_same_v<Member, std::vector<std::string>>) {
                std::vector<std::string> v;
                for (const auto& it : node[name]) v.push_back(it.as<std::string>());
                (params.*ptr) = std::move(v);
            } else {
                // 其他类型暂不支持，保持现值
            }
        } catch (...) {
            // 类型不匹配或转换失败时忽略该键
        }
    });
}

// 新的递归加载器
template <class T>
static void load_params_recursive(const std::string& name, T& params, const std::string& root_dir) {
    YAML::Node node;
    if constexpr (std::is_base_of_v<AnimalParams, T>) {
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Begin recursive load: '{}' (Animal)", name);
        node = load_animal_yaml(name, root_dir);
    } else if constexpr (std::is_base_of_v<PlantParams, T>) {
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Begin recursive load: '{}' (Plant)", name);
        node = load_plant_yaml(name, root_dir);
    } else {
        // 仅支持 AnimalParams / PlantParams
        throw std::runtime_error("Unsupported params type when loading YAML for '" + name + "'");
    }
    if (!node) {
        throw std::runtime_error("Empty YAML content for '" + name + "'");
    }

    if (node["parent"]) {
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] '{}' inherits from '{}'", name, node["parent"].as<std::string>());
        load_params_recursive(node["parent"].as<std::string>(), params, root_dir);
    }

    apply_yaml_fields_by_name(node["species"], params);

    if constexpr (std::is_base_of_v<AnimalParams, T>) {
        parse_pathfinding_params_node(node["species"], params.pathfinding);
        apply_yaml_fields_by_name(node["animal"], params);
        parse_pathfinding_params_node(node["animal"], params.pathfinding);
        // 解析 bt_params（按层覆盖）
        parse_bt_params_node(node["species"], params);
        parse_bt_params_node(node["animal"], params);
    }
    if constexpr (std::is_base_of_v<PlantParams, T>) {
        apply_yaml_fields_by_name(node["plant"], params);
        apply_yaml_fields_by_name(node["grass"], params);
    }

    apply_yaml_fields_by_name(node[name], params);
    if constexpr (std::is_base_of_v<AnimalParams, T>) {
        parse_bt_params_node(node[name], params);
        parse_pathfinding_params_node(node[name], params.pathfinding);
    }
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Applied overrides for '{}'", name);
}

// 可选的物种级后处理（约束修正等）
template <class T>
static void postprocess_params(T&) {}

static void clamp(double& x, double lo, double hi) {
    if (x < lo) x = lo; else if (x > hi) x = hi;
}

template <> inline void postprocess_params<AnimalParams>(AnimalParams& params) {
    clamp(params.hunting_success_rate, 0.0, 1.0);
    if (params.movement_speed < 0.0) params.movement_speed = 0.0;
    if (params.energy_consumption < 0) params.energy_consumption = 0;
    // 饥饿伤害与间隔比例、营养值
    if (params.starvation_damage < 0.0) params.starvation_damage = 0.0;
    clamp(params.starvation_damage_interval_ratio, 0.0, 1.0);
    if (params.nutrition_value < 0.0) params.nutrition_value = 0.0;
    //ENERGY加成参数：
    if (params.nutrition_bonus_max < 0.0) params.nutrition_bonus_max = 0.0;
    if (params.nutrition_bonus_curve_alpha < 0.0) params.nutrition_bonus_curve_alpha = 0.0;
    // 生命恢复：基础恢复量
    if (params.hp_regen_base_per_day < 0.0) params.hp_regen_base_per_day = 0.0;
    // 生命恢复：状态倍数
    if (params.hp_regen_mul_satisfied < 0.0) params.hp_regen_mul_satisfied = 0.0;
    if (params.hp_regen_mul_normal < 0.0) params.hp_regen_mul_normal = 0.0;
    if (params.hp_regen_mul_starving < 0.0) params.hp_regen_mul_starving = 0.0;
    // 生命恢复：触发间隔比例
    clamp(params.regan_interval_ratio, 0.0, 1.0);
    // 攻击伤害区间约束：保证非负与 min<=max，优先于默认伤害
    if (params.attack_damage_min < 0.0) params.attack_damage_min = 0.0;
    if (params.attack_damage_max < 0.0) params.attack_damage_max = 0.0;
    if (params.attack_damage_max < params.attack_damage_min) {
        params.attack_damage_max = params.attack_damage_min;
    }
    // 新生获得能量比例
    clamp(params.newborn_energy_ratio, 0.0, 1.0);
    // 孕期能耗倍率
    if (params.pregnant_energy_multiplier < 0.0) params.pregnant_energy_multiplier = 0.0;

    if (params.pathfinding.replan_interval < 0) {
        params.pathfinding.replan_interval = 0;
    }
    if (!std::isfinite(params.pathfinding.budget_multiplier) || params.pathfinding.budget_multiplier < 0.0) {
        params.pathfinding.budget_multiplier = 0.0;
    }
    params.pathfinding.min_traversal_cost = 1.0;
    for (const auto& kv : params.pathfinding.terrain_cost_overrides) {
        if (std::isfinite(kv.second) && kv.second > 0.0) {
            params.pathfinding.min_traversal_cost = std::min(params.pathfinding.min_traversal_cost, kv.second);
        }
    }
}

template <> inline void postprocess_params<PlantParams>(PlantParams& params) {
    // 植物营养值非负
    if (params.nutrition_value < 0.0) params.nutrition_value = 0.0;
    // 累积式繁殖参数约束
    if (params.repro_energy_threshold < 0.0) params.repro_energy_threshold = 0.0;
    if (params.repro_energy_accumulation_rate < 0.0) params.repro_energy_accumulation_rate = 0.0;
    // 子代初始能量比例
    clamp(params.newborn_energy_ratio, 0.0, 1.0);
}

YamlSpeciesConfigProvider::YamlSpeciesConfigProvider(std::string config_root_dir)
    : root_dir(std::move(config_root_dir)) {}

AnimalParams YamlSpeciesConfigProvider::get_animal_params(const std::string& name) const {
    // 先尝试命中缓存
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = animal_cache.find(name);
        if (it != animal_cache.end()) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Animal params cache hit for '{}'", name);
            return it->second;
        }
    }

    AnimalParams params{}; // 使用结构体自身默认作为最终兜底
    load_params_recursive<AnimalParams>(name, params, root_dir);
    postprocess_params(params);

    // 写入缓存（双检）
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto [it, inserted] = animal_cache.emplace(name, params);
        if (!inserted) {
            // 竞争条件下可能已有值，保持已有值即可
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Animal params cache already populated for '{}'", name);
        } else {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Animal params cached for '{}'", name);
        }
    }
    return params;
}

PlantParams YamlSpeciesConfigProvider::get_plant_params(const std::string& name) const {
    // 先尝试命中缓存
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = plant_cache.find(name);
        if (it != plant_cache.end()) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Plant params cache hit for '{}'", name);
            return it->second;
        }
    }

    PlantParams params{};
    load_params_recursive<PlantParams>(name, params, root_dir);
    postprocess_params(params);

    // 写入缓存（双检）
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto [it, inserted] = plant_cache.emplace(name, params);
        if (!inserted) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Plant params cache already populated for '{}'", name);
        } else {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Plant params cached for '{}'", name);
        }
    }
    return params;
}

std::string YamlSpeciesConfigProvider::get_config_root_dir() const {
    return root_dir;
}
// 解析行为树黑板参数 bt_params 并写入 AnimalParams 的字典
static void parse_bt_params_node(const YAML::Node& node, AnimalParams& params) {
    if (!node) return;
    const YAML::Node bp = node["bt_params"];
    if (!bp) return;

    auto try_assign_scalar = [&](const std::string& key, const YAML::Node& v) {
        try { params.bt_params_ints[key] = v.as<int>(); return; } catch (...) {}
        try { params.bt_params_doubles[key] = v.as<double>(); return; } catch (...) {}
        try { params.bt_params_strings[key] = v.as<std::string>(); return; } catch (...) {}
    };

    std::function<void(const YAML::Node&, const std::string&)> parse_any_map = [&](const YAML::Node& m, const std::string& prefix){
        if (!m || !m.IsMap()) return;
        for (auto it : m) {
            const std::string k = it.first.as<std::string>();
            const YAML::Node v = it.second;
            const std::string full_key = prefix.empty() ? k : (prefix + "." + k);
            if (v.IsMap()) {
                parse_any_map(v, full_key);
            } else if (v.IsSequence()) {
                try_assign_scalar(full_key, v);
            } else {
                try_assign_scalar(full_key, v);
            }
        }
    };

    if (bp.IsMap()) {
        const YAML::Node ints = bp["ints"];
        const YAML::Node doubles = bp["doubles"];
        const YAML::Node strings = bp["strings"];
        if (ints && ints.IsMap()) {
            for (auto it : ints) {
                const std::string k = it.first.as<std::string>();
                try { params.bt_params_ints[k] = it.second.as<int>(); } catch (...) {}
            }
        }
        if (doubles && doubles.IsMap()) {
            for (auto it : doubles) {
                const std::string k = it.first.as<std::string>();
                try { params.bt_params_doubles[k] = it.second.as<double>(); } catch (...) {}
            }
        }
        if (strings && strings.IsMap()) {
            for (auto it : strings) {
                const std::string k = it.first.as<std::string>();
                try { params.bt_params_strings[k] = it.second.as<std::string>(); } catch (...) {}
            }
        }

        for (auto it : bp) {
            const std::string k = it.first.as<std::string>();
            if (k == "ints" || k == "doubles" || k == "strings") continue;
            const YAML::Node v = it.second;
            if (v.IsMap()) {
                parse_any_map(v, k);
            } else {
                try_assign_scalar(k, v);
            }
        }
    }
}