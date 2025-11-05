/*
YAML 物种配置提供者实现
从 config/species/*.yaml 加载并解析参数，提供强类型结构体
*/

#include "species_config_provider.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <spdlog/spdlog.h>
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

// 辅助函数：按顺序查找 YAML 文件
static YAML::Node load_species_yaml(const std::string& name, const std::string& root_dir) {
    std::string path = root_dir + "/config/species/" + name + ".yaml";
    try { return YAML::LoadFile(path); } catch (...) {}

    path = root_dir + "/config/species_base/" + name + ".yaml";
    try { return YAML::LoadFile(path); } catch (...) {}

    path = root_dir + "/config/" + name + ".yaml";
    try { return YAML::LoadFile(path); } catch (...) {}

    SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Config] Cannot find YAML file for: {}", name);
    return YAML::Node();
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
    YAML::Node node = load_species_yaml(name, root_dir);
    if (!node) return;

    if (node["parent"]) {
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

AnimalParams YamlSpeciesConfigProvider::get_tiger_params() const {
    AnimalParams params{}; // 使用结构体自身默认作为最终兜底
    load_params_recursive<AnimalParams>("tiger", params, root_dir);
    postprocess_params(params);
    return params;
}

AnimalParams YamlSpeciesConfigProvider::get_cow_params() const {
    AnimalParams params{};
    load_params_recursive<AnimalParams>("cow", params, root_dir);
    postprocess_params(params);
    return params;
}

PlantParams YamlSpeciesConfigProvider::get_grass_params() const {
    PlantParams params{};
    load_params_recursive<PlantParams>("grass", params, root_dir);
    postprocess_params(params);
    return params;
}