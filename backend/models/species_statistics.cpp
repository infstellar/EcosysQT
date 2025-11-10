#include "species_statistics.h"

void SpeciesStatistics::increment(const std::string& species_name, int count) {
    statistics[species_name] += count;
}

void SpeciesStatistics::set_count(const std::string& species_name, int count) {
    statistics[species_name] = count;
}

int SpeciesStatistics::get_count(const std::string& species_name) const {
    auto it = statistics.find(species_name);
    return it != statistics.end() ? it->second : 0;
}

void SpeciesStatistics::reset() {
    statistics.clear();
}
