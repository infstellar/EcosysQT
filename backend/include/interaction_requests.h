#ifndef INTERACTION_H
#define INTERACTION_H

#include <memory>
#include <variant>

class RaceBase;
class ThingBase;
class Animal;

struct AttemptToEatRaceRequest {
    std::shared_ptr<RaceBase> initiator;
    std::shared_ptr<RaceBase> target;
};

struct AttemptToEatThingRequest {
    std::shared_ptr<RaceBase> initiator;
    std::shared_ptr<ThingBase> target;
};

struct AttemptToReproduceRaceRequest {
    std::shared_ptr<RaceBase> parent;
};

struct AttemptToReproduceThingRequest {
    std::shared_ptr<ThingBase> parent;
};

struct AttemptToMateRequest {
    std::shared_ptr<Animal> female;
    std::shared_ptr<Animal> male;
};

using InteractionRequest = std::variant<
    AttemptToEatRaceRequest,
    AttemptToEatThingRequest,
    AttemptToReproduceRaceRequest,
    AttemptToReproduceThingRequest,
    AttemptToMateRequest
>;

#endif // INTERACTION_H