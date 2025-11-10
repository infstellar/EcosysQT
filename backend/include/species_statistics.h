#pragma once

#include <map>
#include <string>

class SpeciesStatistics {
public:
    std::map<std::string, int> statistics;

    void increment(const std::string& species_name, int count = 1);
    void set_count(const std::string& species_name, int count);
    int get_count(const std::string& species_name) const;
    void reset();
};
