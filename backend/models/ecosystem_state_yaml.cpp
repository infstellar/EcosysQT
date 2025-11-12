#include "utils.h"
#include "race_base.h"
#include "thing_base.h"
#include "race_factory.h"
#include "thing_factory.h"
#include <yaml-cpp/yaml.h>
#include <random>

// 基础序列化实现（RaceBase/ThingBase等复杂对象建议只序列化基础属性或ID）
std::string EcosystemStateData::toYaml() const {
    YAML::Node node;
    node["world_width"] = world_width;
    node["world_height"] = world_height;
    node["time_step"] = time_step;
    node["current_day"] = current_day;
    node["current_quadrum"] = current_quadrum;
    node["current_year"] = current_year;
    node["current_hour"] = current_hour;
    node["current_minute"] = current_minute;
    node["current_quadrum_name"] = current_quadrum_name;
    node["current_tps"] = current_tps;

    // 草位置
    std::vector<std::vector<double>> grass_positions;
    for (int i = 0; i < grass_positions_array.rows(); ++i) {
        std::vector<double> pos = {grass_positions_array(i, 0), grass_positions_array(i, 1)};
        grass_positions.push_back(pos);
    }
    node["grass_positions"] = grass_positions;

    // race_lists 基础属性
    YAML::Node races_node;
    for (const auto& [species, individuals] : race_lists) {
        YAML::Node species_node;
        for (const auto& race : individuals) {
            if (!race) continue;
            YAML::Node ind;
            ind["x"] = race->position.x;
            ind["y"] = race->position.y;
            ind["energy"] = race->energy;
            ind["max_energy"] = race->max_energy;
            ind["hp_current"] = race->hp_current;
            ind["hp_max"] = race->hp_max;
            ind["age"] = race->age;
            ind["max_age"] = race->max_age;
            ind["alive"] = race->alive;
            ind["species_name"] = race->species_name;
            species_node.push_back(ind);
        }
        races_node[species] = species_node;
    }
    node["race_lists"] = races_node;

    // thing_lists 基础属性
    YAML::Node things_node;
    for (const auto& [species, things] : thing_lists) {
        YAML::Node species_node;
        for (const auto& thing : things) {
            if (!thing) continue;
            YAML::Node ind;
            ind["x"] = thing->position.x;
            ind["y"] = thing->position.y;
            ind["energy"] = thing->energy;
            ind["max_energy"] = thing->max_energy;
            ind["nutrition_value"] = thing->nutrition_value;
            ind["age"] = thing->age;
            ind["max_age"] = thing->max_age;
            ind["alive"] = thing->alive;
            ind["species_name"] = thing->species_name;
            ind["variant_index"] = thing->variant_index;
            species_node.push_back(ind);
        }
        things_node[species] = species_node;
    }
    node["thing_lists"] = things_node;

    YAML::Emitter out;
    out << node;
    return out.c_str();
}

void EcosystemStateData::fromYaml(const YAML::Node& node) {
    world_width = node["world_width"].as<int>();
    world_height = node["world_height"].as<int>();
    time_step = node["time_step"].as<int>();
    current_day = node["current_day"].as<int>();
    current_quadrum = node["current_quadrum"].as<int>();
    current_year = node["current_year"].as<int>();
    current_hour = node["current_hour"].as<int>();
    current_minute = node["current_minute"].as<int>();
    current_quadrum_name = node["current_quadrum_name"].as<std::string>();
    current_tps = node["current_tps"].as<double>();

    // 草位置
    if (node["grass_positions"]) {
        auto grass_positions = node["grass_positions"];
        grass_positions_array.resize(grass_positions.size(), 2);
        for (size_t i = 0; i < grass_positions.size(); ++i) {
            grass_positions_array(i, 0) = grass_positions[i][0].as<double>();
            grass_positions_array(i, 1) = grass_positions[i][1].as<double>();
        }
    } else {
        grass_positions_array.resize(0,2);
    }

    // 使用工厂恢复 race_lists（确保派生类与行为树等被正确构建）
    race_lists.clear();
    if (node["race_lists"]) {
        auto races_node = node["race_lists"];
        std::mt19937 rng(std::random_device{}());
        for (auto it = races_node.begin(); it != races_node.end(); ++it) {
            std::string species = it->first.as<std::string>();
            std::vector<std::shared_ptr<RaceBase>> individuals;
            for (const auto& ind : it->second) {
                Position pos{ ind["x"].as<double>(), ind["y"].as<double>() };
                std::unique_ptr<RaceBase> created;
                try {
                    created = g_race_factory.create(species, pos, rng);
                } catch (...) {
                    created.reset();
                }

                std::shared_ptr<RaceBase> racePtr;
                if (created) {
                    racePtr = std::move(created);
                } else {
                    // 兜底：构造一个基本 RaceBase（避免空指针）
                    racePtr = std::make_shared<RaceBase>(pos,
                                                         ind["species_name"] ? ind["species_name"].as<std::string>() : species,
                                                         ind["energy"] ? ind["energy"].as<double>() : 100.0,
                                                         ind["max_age"] ? ind["max_age"].as<int>() : 100,
                                                         ind["min_reproduction_energy"] ? ind["min_reproduction_energy"].as<double>() : 50.0,
                                                         ind["hp_max"] ? ind["hp_max"].as<double>() : 100.0);
                }

                // 恢复状态字段（如果存在则赋值）
                if (ind["energy"])      racePtr->energy = ind["energy"].as<double>();
                if (ind["max_energy"])  racePtr->max_energy = ind["max_energy"].as<double>();
                if (ind["hp_current"])  racePtr->hp_current = ind["hp_current"].as<double>();
                if (ind["hp_max"])      racePtr->hp_max = ind["hp_max"].as<double>();
                if (ind["age"])         racePtr->age = ind["age"].as<int>();
                if (ind["max_age"])     racePtr->max_age = ind["max_age"].as<int>();
                if (ind["alive"])       racePtr->alive = ind["alive"].as<bool>();
                if (ind["species_name"])racePtr->species_name = ind["species_name"].as<std::string>();
                

                individuals.push_back(racePtr);
            }
            race_lists[species] = std::move(individuals);
        }
    }

    // thing_lists 基础属性（同样使用工厂恢复）
    thing_lists.clear();
    if (node["thing_lists"]) {
        auto things_node = node["thing_lists"];
        std::mt19937 rng(std::random_device{}());
        for (auto it = things_node.begin(); it != things_node.end(); ++it) {
            std::string species = it->first.as<std::string>();
            std::vector<std::shared_ptr<ThingBase>> things;
            for (const auto& ind : it->second) {
                Position pos{ ind["x"].as<double>(), ind["y"].as<double>() };
                std::unique_ptr<ThingBase> created;
                try {
                    created = g_thing_factory.create(species, pos, rng);
                } catch (...) {
                    created.reset();
                }

                std::shared_ptr<ThingBase> thingPtr;
                if (created) {
                    thingPtr = std::move(created);
                } else {
                    thingPtr = std::make_shared<ThingBase>(pos,
                                                          ind["energy"] ? ind["energy"].as<double>() : 100.0,
                                                          ind["max_age"] ? ind["max_age"].as<int>() : 100,
                                                          ind["min_reproduction_energy"] ? ind["min_reproduction_energy"].as<double>() : 50.0);
                }

                if (ind["energy"])           thingPtr->energy = ind["energy"].as<double>();
                if (ind["max_energy"])       thingPtr->max_energy = ind["max_energy"].as<double>();
                if (ind["nutrition_value"])  thingPtr->nutrition_value = ind["nutrition_value"].as<double>();
                if (ind["age"])              thingPtr->age = ind["age"].as<int>();
                if (ind["max_age"])          thingPtr->max_age = ind["max_age"].as<int>();
                if (ind["alive"])            thingPtr->alive = ind["alive"].as<bool>();
                if (ind["species_name"])     thingPtr->species_name = ind["species_name"].as<std::string>();
                if (ind["variant_index"])    thingPtr->variant_index = ind["variant_index"].as<int>();

                things.push_back(thingPtr);
            }
            thing_lists[species] = std::move(things);
        }
    }
}