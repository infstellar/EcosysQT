/*
YAML 物种配置提供者实现
从 config/species/*.yaml 加载并解析参数，提供强类型结构体
*/

#include "species_config_provider.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <spdlog/spdlog.h>
// 移除 <filesystem>，引入 Qt 模块
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
// 反射: 成员名与继承枚举
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <type_traits>

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
    const QString search_root = QString::fromStdString(root_dir + "/config");
    std::string first_match;
    std::string preferred_match;

    QDirIterator it(search_root, QStringList() << (QString::fromStdString(name) + ".yaml"), QDir::Files, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString file_path = it.next();
        std::string full_path_str = file_path.toStdString();

        if (first_match.empty()) {
            first_match = full_path_str;
        }

        if (preferred_subfolder) {
            // 使用 QDir 来检查路径是否包含特定子文件夹，更健壮
            QDir dir(file_path);
            dir.cdUp(); // 移动到文件所在的目录
            if (dir.dirName() == QString::fromStdString(preferred_subfolder)) {
                 preferred_match = full_path_str;
                 break; // 找到最优匹配，可以提前退出
            }
            // 兼容旧的字符串查找方式作为后备
            const std::string slash = std::string("/") + preferred_subfolder + "/";
            const std::string backslash = std::string("\\") + preferred_subfolder + "\\";
            if (full_path_str.find(slash) != std::string::npos || full_path_str.find(backslash) != std::string::npos) {
                preferred_match = full_path_str;
            }
        }
    }

    return !preferred_match.empty() ? preferred_match : first_match;
}

static YAML::Node load_yaml_in_category(const std::string& name, const std::string& root_dir, const char* category) {
    const std::string primary = root_dir + std::string("/config/species/") + category + "/" + name + ".yaml";
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Loading '{}' YAML for '{}' from '{}'", category, name, primary);
    try {
        return YAML::LoadFile(primary);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Config] Primary '{}' YAML not found for '{}': {}. Begin broad search.", category, name, e.what());
        const std::string found = search_yaml_path(name, root_dir, category);
        if (!found.empty()) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[Config] Found '{}' via broad search at '{}'", name, found);
            try {
                return YAML::LoadFile(found);
            } catch (const std::exception& e2) {
                SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] Failed to load broad-searched '{}' YAML for '{}': {}", category, name, e2.what());
                throw std::runtime_error("Failed to load YAML '" + name + "' at '" + primary + "' and searched '" + found + "': " + e2.what());
            }
        }
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] '{}' YAML for '{}' not found after broad search", category, name);
        throw std::runtime_error("Failed to locate YAML '" + name + "' from '" + primary + "' or anywhere under '" + (root_dir + "/config") + "'");
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
        apply_yaml_fields_by_name(node["animal"], params);
    }
    if constexpr (std::is_base_of_v<PlantParams, T>) {
        apply_yaml_fields_by_name(node["plant"], params);
        apply_yaml_fields_by_name(node["grass"], params);
    }

    apply_yaml_fields_by_name(node[name], params);
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
}

YamlSpeciesConfigProvider::YamlSpeciesConfigProvider(std::string config_root_dir)
    : root_dir(std::move(config_root_dir)) {}

AnimalParams YamlSpeciesConfigProvider::get_animal_params(const std::string& name) const {
    AnimalParams params{}; // 使用结构体自身默认作为最终兜底
    load_params_recursive<AnimalParams>(name, params, root_dir);
    postprocess_params(params);
    return params;
}

PlantParams YamlSpeciesConfigProvider::get_plant_params(const std::string& name) const {
    PlantParams params{};
    load_params_recursive<PlantParams>(name, params, root_dir);
    postprocess_params(params);
    return params;
}

std::string YamlSpeciesConfigProvider::get_config_root_dir() const {
    return root_dir;
}