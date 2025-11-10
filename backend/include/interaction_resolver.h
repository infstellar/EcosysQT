#ifndef INTERACTION_RESOLVER_H
#define INTERACTION_RESOLVER_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "interaction_requests.h"

class EcosystemState;
class RaceBase;
class ThingBase;

struct InteractionResolutionState {
    std::unordered_map<RaceBase*, double> race_energy_changes;
    std::unordered_set<RaceBase*> race_marked_for_death;
    std::unordered_map<ThingBase*, double> thing_energy_changes;
    std::unordered_set<ThingBase*> thing_marked_for_death;
    std::vector<std::shared_ptr<RaceBase>> reproduction_parents;
    std::vector<std::shared_ptr<ThingBase>> thing_reproduction_parents;

    void clear();
};

class InteractionResolver {
public:
    InteractionResolver() = default;

    template <typename RequestContainer>
    void process_requests(const RequestContainer& requests,
                          EcosystemState& state,
                          InteractionResolutionState& results) {
        for (const auto& request : requests) {
            dispatch_request(request, state, results);
        }
    }

private:
    void dispatch_request(const InteractionRequest& request,
                          EcosystemState& state,
                          InteractionResolutionState& results);

    void handle_request(const AttemptToEatThingRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);

    void handle_request(const DamageRaceRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);

    void handle_request(const DamageThingRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);

    void handle_request(const AttemptToReproduceRaceRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);

    void handle_request(const AttemptToReproduceThingRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);

    void handle_request(const AttemptToMateRequest& req,
                        EcosystemState& state,
                        InteractionResolutionState& results);
};

#endif // INTERACTION_RESOLVER_H
