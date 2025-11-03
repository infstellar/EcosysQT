## 功能需求实现情况分析 (已优化)

以下是当前项目的实现情况及优化后的行动计划。

### ✅ 已实现的功能

**1. 动物具备可量化的饥饿值**
- **实现位置**: `Species` 基类和 `Animal` 类
- **饥饿值量化**: 通过 `energy` 属性量化，`max_energy` 定义最大能量值
- **消耗机制**: 每次 `update()` 调用时扣除 `energy_consumption`

### ❌ 未完整实现的功能

**1. 饱食状态判定** - **未实现**
- 当前只有简单的能量值，没有状态分类逻辑
- 缺少"吃饱"、"正常"、"低功耗"三种状态的定义和判定

**2. 捕食意愿调控机制** - **部分实现**
- **已有**: 老虎在能量低时会提高 `hunting_success_rate`（tiger.cpp 第32-36行）
- **缺少**: 系统性的捕食意愿与能量关联机制

**3. 动态能量消耗机制** - **未实现**
- 当前能量消耗是固定值 `energy_consumption`
- 缺少基于状态的动态消耗调整

**4. 状态转换逻辑** - **未实现**
- 缺少状态之间的转换逻辑
- 缺少基于状态的行为调整（如逛街机制）

---

## 优化后的行动方案

### 1. 为 Animal 类添加饱食状态系统 (State Machine)

**实现位置**: `backend/include/species.h` 和 `backend/models/animal.cpp`

**需要添加的内容**:

1.  **在 `species.h` 中添加状态枚举**:
    ```cpp
    // 动物饱食状态枚举
    enum class HungerState {
        SATISFIED,   // 吃饱了，低欲望，倾向于随机移动（逛街）
        NORMAL,      // 正常状态，根据物种习性决定行为
        STARVING     // 饥饿状态，高欲望，主动寻找食物
    };
    ```
    > **[优化建议 - 解耦]**: 将状态命名为 `SATISFIED`, `NORMAL`, `STARVING` 更能体现状态本质，而不是行为（如“低功耗”）。这使得状态定义更纯粹，与后续的行为决策解耦。

2.  **在 `Animal` 类中添加新成员**:
    ```cpp
    // 在 Animal 类 (species.h) 的 protected 部分添加
    protected:
        HungerState hunger_state;
        double satisfied_threshold;    // 吃饱阈值（基于最大能量的比例）
        double starving_threshold;     // 饥饿阈值（基于最大能量的比例）
        double base_movement_speed;    // 基础移动速度
        double base_energy_consumption; // 基础能量消耗
        bool is_wandering;             // 是否处于逛街状态
        int wandering_cooldown;        // 逛街冷却/持续时间
    ```

3.  **在 `Animal` 类中添加状态管理方法**:
    ```cpp
    // public 或 protected 部分
    public:
        // 获取当前捕食意愿（0-1），应为虚函数以支持不同动物的特殊逻辑
        virtual double get_hunting_desire() const;

    protected:
        // 更新饱食状态，应为 private 或 protected
        void update_hunger_state();

        // 根据状态调整能耗和速度，应为 private 或 protected
        void adjust_stats_by_state();
    ```
    > **[优化建议 - 代码复用与解耦]**
    > - **移除 `wander_move`**: “逛街”本质上是在满足特定状态（吃饱）下的随机移动。现有的 `move_randomly` 方法完全可以复用。我们不需要一个新的 `wander_move` 方法，只需在 `update` 循环中根据 `is_wandering` 状态调用 `move_randomly` 即可。
    > - **接口设计**: 将 `get_hunting_desire()` 设计为 `public virtual`，这形成了一个清晰的接口，`update` 逻辑依赖于这个接口做决策，而具体动物（如老虎）可以重写它，实现自身独特的欲望变化曲线，完美实现了解耦。
    > - **封装**: `update_hunger_state` 和 `adjust_stats_by_state` 是内部逻辑，应设为 `protected` 或 `private`，避免外部直接调用。

### 2. 修改 Animal 构造函数和参数

**实现位置**: `backend/include/species_params.h` 和 `backend/models/animal.cpp`

**需要添加的参数**:
```cpp
// 在 AnimalParams 结构体中添加
struct AnimalParams {
    // ... 现有参数
    double satisfied_threshold_ratio = 0.8;   // 吃饱阈值比例
    double starving_threshold_ratio = 0.2;    // 饥饿阈值比例
    int wandering_duration = 50;              // 逛街持续时间
};
```
> **[实现建议]**: 确保 `Animal` 的构造函数会接收 `AnimalParams`，并使用这些新参数来初始化 `satisfied_threshold` (`max_energy * ratio`) 和 `starving_threshold` 等成员变量。

### 3. 重写 Animal::update() 方法 (核心架构优化)

**实现位置**: `backend/models/animal.cpp`

**核心逻辑**:
```cpp
// animal.cpp
void Animal::update(const EcosystemState& ecosystem_state) {
    // 1. 调用基类更新，处理年龄等通用逻辑
    Species::update(ecosystem_state);
    if (!is_alive()) return;

    // 2. 更新内部状态
    update_hunger_state();
    adjust_stats_by_state();

    // 3. 根据状态和意愿决定行为
    if (is_wandering) {
        // 复用现有代码：逛街就是随机移动
        move_randomly(ecosystem_state.config.world_width, ecosystem_state.config.world_height, movement_speed);
    } else if (get_hunting_desire() > 0.3) { // 用接口判断意愿
        // 复用现有代码：调用已有的智能移动
        intelligent_move(ecosystem_state);
    } else {
        // 默认行为：低欲望时也进行随机移动
        move_randomly(ecosystem_state.config.world_width, ecosystem_state.config.world_height, movement_speed);
    }

    // 4. 扣除动态计算后的能量
    energy -= energy_consumption;

    // 5. 更新“逛街”状态的计时器
    if (wandering_cooldown > 0) {
        wandering_cooldown--;
        if (wandering_cooldown == 0) {
            is_wandering = false;
        }
    }
}
```
> **[优化建议 - 架构与复用]**
> - **引入中间层**: 这是最重要的架构优化。当前具体动物类（`Tiger`）直接调用 `Species::update`。我们应在 `Animal` 类中实现上述 `update` 方法，作为所有动物的通用行为逻辑。
> - **调用链**: 未来的调用链应为：`Tiger::update()` -> `Animal::update()` -> `Species::update()`。这使得 `Animal` 成为了一个行为逻辑的中间层，极大提高了代码复用性。
> - **复用**: 如上所示，行为决策完全复用了已有的 `intelligent_move` 和 `move_randomly` 方法，无需编写新函数。

### 4. 修改具体动物类的 update() 方法

**实现位置**: `cow.cpp` 和 `tiger.cpp`

**修改要点**:
```cpp
// 以 tiger.cpp 为例
void Tiger::update(const EcosystemState& ecosystem_state) {
    // 1. 首先调用 Animal 的通用 update 逻辑
    Animal::update(ecosystem_state);
    if (!is_alive()) return;

    // 2. 执行 Tiger 的特有行为，如捕食
    // 检查捕食意愿，而不是无脑捕食
    if (get_hunting_desire() > 0.5) {
        // eat() 方法内部应包含寻找和攻击逻辑
        // eat() 成功后，应在内部设置 is_wandering = true 并重置冷却
        eat(ecosystem_state.species);
    }
}

// 在 Tiger::eat() 或类似方法中
void Tiger::eat(...) {
    // ... 捕食成功 ...
    if (prey_eaten) {
        energy += prey_energy;
        // 吃饱后，触发“逛街”状态
        is_wandering = true;
        wandering_cooldown = wandering_duration; // wandering_duration 从配置中读取
    }
}
```
> **[优化建议 - 解耦与职责单一]**
> - `Tiger::update` 的职责应简化为：调用父类通用逻辑 + 执行自身特殊逻辑（如 `eat`）。
> - 将“吃饱后开始逛街”的逻辑放在 `eat` 方法成功捕食之后，符合“行为导致状态变更”的原则，职责更清晰。

### 5. 更新配置文件

**实现位置**: `config/species/cow.yaml` 和 `config/species/tiger.yaml`

**需要添加的配置**:
```yaml
animal:
  # ... 现有配置 ...
  satisfied_threshold_ratio: 0.8
  starving_threshold_ratio: 0.2
  wandering_duration: 50
```
> **[实现建议]**: 确保 `SpeciesConfigProviderYaml` 类能够正确解析这些新增的 `animal` 层级下的参数，并填充到 `AnimalParams` 结构体中。

---

### 6. 质量保证与测试建议 (新增)

> **[优化建议 - 质量保证]**
> 为确保方案的稳健性，应在开发过程中同步考虑测试。
>
> 1.  **单元测试**:
>     - **`Animal::update_hunger_state()`**: 编写测试用例，给定不同的 `energy` 和 `max_energy`，断言 `hunger_state` 是否切换到预期的 `SATISFIED`, `NORMAL`, `STARVING`。
>     - **`Animal::get_hunting_desire()`**: 测试在不同状态下，捕食意愿的返回值是否符合预期。
>
> 2.  **集成测试**:
>     - **完整生命周期测试**: 创建一个测试场景，让一个动物从“吃饱”开始，能量逐渐下降，观察其行为是否从“逛街”（随机移动）切换到“主动觅食”（智能移动）。
>     - **配置加载测试**: 验证 `cow.yaml` 和 `tiger.yaml` 中的新参数是否被正确加载并影响动物的行为。
>
> 3.  **代码审查**:
>     - 重点检查 `Animal::update` 是否被 `Tiger` 和 `Cow` 正确调用。
>     - 确认 `move_randomly` 和 `intelligent_move` 被成功复用。
>     - 检查是否存在不必要的 public 成员变量，优先使用 private/protected。

这个优化后的计划通过引入 `Animal` 行为中间层和复用现有移动逻辑，大大增强了代码的复用性和扩展性，同时通过清晰的接口和职责划分改善了代码的解耦。新增的测试建议将为功能的正确性提供保障。