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

// 逃离威胁：根据黑板中的威胁位置，计算反方向安全点并执行一步移动，清理繁殖相关上下文
bt::Status FleeFromThreat(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 游荡：采样/推进游荡目标，按黑板倍率执行一步移动
bt::Status WanderAnywhere(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 选择可用配偶并在范围内提交交配请求；否则锁定意图并向配偶位置前进
bt::Status ApproachOrMate(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 近场吃东西（如 grass）：在范围内提交吃东西请求并跳过移动
bt::Status EatNearbyThing(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 近场捕食指定物种（如 tiger 捕食 cow）：命中概率后提交请求并进入冷却
bt::Status HuntNearbyRace(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 选择最近的可食目标点（race/things），写入当前移动目标
bt::Status SelectTargetPoint(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

// 规划路径到当前目标并执行一步移动（支持黑板速度/能耗倍率）
bt::Status PlanPathToTarget(Animal& self, bt::TickContext& ctx, const YAML::Node& params);

} // namespace behavior::actions