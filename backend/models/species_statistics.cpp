#include "species_statistics.h"

SpeciesStatistics::SpeciesStatistics() {
    for (auto type : {SpeciesType::GRASS, SpeciesType::COW, SpeciesType::TIGER}) {
        statistics[type] = 0;
    }
}

void SpeciesStatistics::increment(SpeciesType type, int count) {
    statistics[type] += count;
}

void SpeciesStatistics::set_count(SpeciesType type, int count) {
    statistics[type] = count;
}

int SpeciesStatistics::get_count(SpeciesType type) const {
    auto it = statistics.find(type);
    return it != statistics.end() ? it->second : 0;
}

void SpeciesStatistics::reset() {
    for (auto& kv : statistics) {
        kv.second = 0;
    }
}

int SpeciesStatistics::grass() const {
    return get_count(SpeciesType::GRASS);
}

void SpeciesStatistics::set_grass(int value) {
    set_count(SpeciesType::GRASS, value);
}

int SpeciesStatistics::cow() const {
    return get_count(SpeciesType::COW);
}

void SpeciesStatistics::set_cow(int value) {
    set_count(SpeciesType::COW, value);
}

int SpeciesStatistics::tiger() const {
    return get_count(SpeciesType::TIGER);
}

void SpeciesStatistics::set_tiger(int value) {
    set_count(SpeciesType::TIGER, value);
}
