#pragma once

#include <map>
#include "species.h"

class SpeciesStatistics {
public:
    std::map<SpeciesType, int> statistics;

    SpeciesStatistics();
    void increment(SpeciesType type, int count = 1);
    void set_count(SpeciesType type, int count);
    int get_count(SpeciesType type) const;
    void reset();

    int grass() const;
    void set_grass(int value);
    int cow() const;
    void set_cow(int value);
    int tiger() const;
    void set_tiger(int value);
};
