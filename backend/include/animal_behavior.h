/*
动物行为模块 - 行为树构建与组织
将动物行为的节点拓扑与行动/条件绑定从 Animal 类中抽离，
以“单一职责 + 清晰结构”的方式组织代码，遵循 task6_re_2.md 的方案：
PrioritySelector 根，三大分支：交配（雄性触发）、觅食/捕食、游荡。
*/

#pragma once

#include <memory>

namespace bt { class BehaviorTree; }
class Animal;

namespace behavior {

// 为指定动物构建行为树（代码构建版，后续可替换为配置驱动）
std::unique_ptr<bt::BehaviorTree> build_tree_for_animal(Animal& self);

} // namespace behavior