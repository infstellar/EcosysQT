/*
行为树动作封装模块
将具体的行为逻辑（逃跑、游荡、追逐、交配、近场吃草/捕食、路径追逐等）抽象为可复用函数，
以消除 YAML 工厂与代码版行为树中的逻辑重复。
*/
#pragma once

#include <yaml-cpp/yaml.h>
#include "behavior_tree.h"

class Animal;

namespace behavior::actions {

// 游荡目标选择：挑选可达随机点并写入黑板
bt::Status SelectWanderTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 近距离交配尝试：在交配范围内查找可用雌性并提交交互请求；不包含移动
bt::Status AttemptToMate(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 吃黑板上的目标（如 grass）：在范围内提交吃东西请求并跳过移动
bt::Status EatTargetThing(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 捕食指定物种：依赖黑板目标，在攻击范围内完成本地验证后提交请求
bt::Status HuntTargetRace(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 选择最近的可食目标点（race/things），写入当前移动目标
bt::Status SelectTargetPoint(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 直接使用路径代价搜索最近的 Thing 并沿路径推进
bt::Status SeekThingWithPath(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 选择逃逸目的地：根据威胁反方向与采样偏角，写入长距离目标
bt::Status SelectFleeDestination(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 规划路径到当前目标并执行一步移动（支持黑板速度/能耗倍率）
bt::Status PlanPathToTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 清理黑板上的移动目标
bt::Status ClearBlackboardTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 更新已锁定配偶的最新位置
bt::Status UpdateMateTargetPosition(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 更新已锁定猎物的最新位置
bt::Status UpdateHuntTargetPosition(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

} // namespace behavior::actions