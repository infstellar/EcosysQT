#ifndef INTERACTION_H
#define INTERACTION_H

#include <memory>
#include <variant>

class RaceBase;
class ThingBase;
class Animal;

struct AttemptToEatThingRequest {
    std::shared_ptr<RaceBase> initiator;
    std::shared_ptr<ThingBase> target;
};

// 新伤害系统：对实体造成伤害（Race/Thing）
struct DamageRaceRequest {
    std::shared_ptr<RaceBase> attacker;
    std::shared_ptr<RaceBase> target;
    double damage = 0.0;
    // 调试：记录此次攻击采样区间，便于 UI 审计
    double damage_min = 0.0;
    double damage_max = 0.0;
};

struct DamageThingRequest {
    std::shared_ptr<RaceBase> attacker;
    std::shared_ptr<ThingBase> target;
    double damage = 0.0;
    double damage_min = 0.0;
    double damage_max = 0.0;
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
    AttemptToEatThingRequest,
    DamageRaceRequest,
    DamageThingRequest,
    AttemptToReproduceRaceRequest,
    AttemptToReproduceThingRequest,
    AttemptToMateRequest
>;

#endif // INTERACTION_H