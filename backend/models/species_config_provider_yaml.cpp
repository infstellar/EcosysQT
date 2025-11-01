/*
YAML 物种配置提供者实现
从 config/species/*.yaml 加载并解析参数，提供强类型结构体
*/

#include "species_config_provider.h"
#include <yaml-cpp/yaml.h>
#include <string>
#include <iostream>
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
        std::cerr << "[Config] Failed to load YAML: " << p << ", error: " << e.what() << std::endl;
        return YAML::Node();
    }
}

template <typename T>
static T read_scalar_or_default(const YAML::Node& node, const std::string& key, const T& def) {
    if (node && node[key]) {
        try { return node[key].as<T>(); } catch (...) {}
    }
    return def;
}

static std::vector<std::string> read_string_list_or_default(const YAML::Node& node, const std::string& key, const std::vector<std::string>& def) {
    if (node && node[key]) {
        try {
            std::vector<std::string> v;
            for (const auto& it : node[key]) v.push_back(it.as<std::string>());
            return v;
        } catch (...) {}
    }
    return def;
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

// 分层 apply：基础通用字段
static void apply_species_base(const YAML::Node& base, SpeciesBaseParams& params) {
    apply_yaml_fields_by_name(base, params);
}

// 分层 apply：动物通用行为字段
static void apply_animal_common(const YAML::Node& behavior, AnimalParams& params) {
    apply_yaml_fields_by_name(behavior, params);
}

// 编译期通用：按继承层次应用配置（species→animal→具体物种）
template <class T>
static void apply_inheritance_layers(const std::string& root_dir, const char* species_key, T& params) {
    // 1) 父类默认：species.yaml / species.species
    {
        std::string sp = root_dir + "/config/species.yaml";
        YAML::Node sroot = load_yaml_file(sp);
        YAML::Node s = sroot["species"];
        apply_yaml_fields_by_name(s["species"], params);
    }
    // 2) 动物默认（仅当 T 继承 AnimalParams）：animal.yaml / animal.animal
    if constexpr (std::is_base_of_v<AnimalParams, T>) {
        std::string ap = root_dir + "/config/animal.yaml";
        YAML::Node aroot = load_yaml_file(ap);
        YAML::Node a = aroot["animal"];
        apply_yaml_fields_by_name(a["animal"], params);
    }
    // 3) 物种专属：config/species/<species_key>.yaml （species / animal / <species_key>）
    {
        std::string p = root_dir + "/config/species/" + std::string(species_key) + ".yaml";
        YAML::Node root = load_yaml_file(p);
        YAML::Node spnode = root[species_key];
        apply_yaml_fields_by_name(spnode["species"], params);
        if constexpr (std::is_base_of_v<AnimalParams, T>) {
            apply_yaml_fields_by_name(spnode["animal"], params);
        }
        apply_yaml_fields_by_name(spnode[species_key], params);
    }
}

// 可选的物种级后处理（约束修正等）
template <class T>
static void postprocess_params(T&) {}

static void clamp(double& x, double lo, double hi) {
    if (x < lo) x = lo; else if (x > hi) x = hi;
}

template <> inline void postprocess_params<TigerParams>(TigerParams& params) {
    clamp(params.hunting_success_rate, 0.0, 1.0);
    if (params.movement_speed < 0.0) params.movement_speed = 0.0;
    if (params.energy_consumption < 0) params.energy_consumption = 0;
}

YamlSpeciesConfigProvider::YamlSpeciesConfigProvider(std::string config_root_dir)
    : root_dir(std::move(config_root_dir)) {}

TigerParams YamlSpeciesConfigProvider::get_tiger_params() const {
    TigerParams params{}; // 使用结构体自身默认作为最终兜底
    apply_inheritance_layers<TigerParams>(root_dir, "tiger", params);
    postprocess_params(params);
    return params;
}

CowParams YamlSpeciesConfigProvider::get_cow_params() const {
    CowParams params{};
    apply_inheritance_layers<CowParams>(root_dir, "cow", params);
    postprocess_params(params);
    return params;
}

GrassParams YamlSpeciesConfigProvider::get_grass_params() const {
    GrassParams params{};
    apply_inheritance_layers<GrassParams>(root_dir, "grass", params);
    postprocess_params(params);
    return params;
}