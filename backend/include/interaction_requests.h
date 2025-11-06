#ifndef INTERACTION_H
#define INTERACTION_H

#include <variant>
#include <memory>

// 前向声明，避免引入完整 species 头
class Species;
class Animal; // 新增前向声明
// 一个物种尝试吃另一个物种的请求（统一“吃”的动作）
struct AttemptToEatRequest {
    std::shared_ptr<Species> initiator; // 发起者：老虎/牛/鹿等
    std::shared_ptr<Species> target;    // 目标：牛/草/鹿等
};

// 一个物种尝试繁殖的请求
struct AttemptToReproduceRequest {
    std::shared_ptr<Species> parent;    // 发起繁殖的个体
};
// --- 新增：一个雌性动物尝试与雄性动物交配的请求 ---
struct AttemptToMateRequest {
    std::shared_ptr<Animal> female;
    std::shared_ptr<Animal> male;
};
// 所有交互请求的统一变体类型
using InteractionRequest = std::variant<
    AttemptToEatRequest,
    AttemptToReproduceRequest,
    AttemptToMateRequest // 新增
>;

#endif // INTERACTION_H