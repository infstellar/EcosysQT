#ifndef POPULATION_MANAGER_H
#define POPULATION_MANAGER_H

class EcosystemState;

class PopulationManager {
public:
    PopulationManager() = default;

    void apply_changes(EcosystemState& state);
};

#endif // POPULATION_MANAGER_H
