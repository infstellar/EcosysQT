这是一个简明、高效的均匀网格（Spatial Hash）执行方案，它将完美契合我们之前讨论的多线程架构。

此方案的核心是将物种的空间信息\*\*“烘焙”\*\*到一个2D网格中，允许并发的“决策”阶段以 `O(1)` 的复杂度快速查询，而不是 `O(N)` 遍历。

-----

### 均匀网格 (Spatial Hash) 执行方案

#### 阶段 1：在 `EcosystemState` 中定义网格

**目标：** `EcosystemState` 需要拥有这个网格，并提供一个**可并发读取**的接口。

**1.1. 在 `ecosystem.h` 中添加网格数据结构：**

在 `EcosystemState` 类的 `private` 部分添加：

```cpp
#include <vector>
#include <memory>

class Species; // 已有前向声明

// ...
private:
    // 网格本身：一个2D数组，每个单元格(Cell)包含一个物种指针列表
    std::vector<std::vector<std::vector<std::shared_ptr<Species>>>> spatial_grid;

    // 网格参数
    double cell_size;
    int grid_width;
    int grid_height;
```

**1.2. 在 `EcosystemState::EcosystemState` (构造函数) 中初始化网格：**

在 `ecosystem.cpp` 的构造函数中：

```cpp
EcosystemState::EcosystemState(const EcosystemConfig& config)
    : config(config), time_step(0), species_registry(config), /*...*/ {

    // *** 新增网格初始化 ***
    // 关键：选择一个合适的单元格尺寸。
    // 理想值 = 动物的最大 detection_range
    // 假设我们选择 100.0 (你需要根据参数调整)
    this->cell_size = 100.0; 
    
    this->grid_width = static_cast<int>(std::ceil(config.world_width / cell_size));
    this->grid_height = static_cast<int>(std::ceil(config.world_height / cell_size));

    // 调整网格大小以匹配维度
    spatial_grid.resize(grid_width, 
        std::vector<std::vector<std::shared_ptr<Species>>>(grid_height)
    );
    // ***********************

    initialize_populations();
}
```

#### 阶段 2：在 `prepare_for_update` (串行阶段) 中构建网格

**目标：** 在每帧的**串行阶段 1**，用所有物种的最新位置**重建**网格。

**2.1. 修改 `EcosystemState::prepare_for_update` (在 `ecosystem.cpp` 中)：**

```cpp
void EcosystemState::prepare_for_update() {
    // ... (清理旧的请求队列等) ...

    // *** 新增网格重建 ***
    
    // 1. 清空所有单元格 (O(N) - N为单元格数量)
    for (int x = 0; x < grid_width; ++x) {
        for (int y = 0; y < grid_height; ++y) {
            spatial_grid[x][y].clear();
        }
    }

    // 2. 填充所有物种 (O(M) - M为物种数量)
    for (const auto& name : species_registry.get_all_species_names()) {
        for (auto& individual : species_registry.get_species_list(name)) {
            if (!individual->alive) continue;

            // O(1) 哈希计算
            int cell_x = static_cast<int>(individual->position.x / cell_size);
            int cell_y = static_cast<int>(individual->position.y / cell_size);

            // 边界检查
            cell_x = std::max(0, std::min(cell_x, grid_width - 1));
            cell_y = std::max(0, std::min(cell_y, grid_height - 1));

            // O(1) 插入
            spatial_grid[cell_x][cell_y].push_back(individual);
        }
    }
    // ***********************
}
```

#### 阶段 3：实现 `get_nearby_species` (并发读取)

**目标：** 创建一个新的、线程安全的查询函数，供**并发阶段 2 (决策)** 调用。

**3.1. 在 `ecosystem.h` 中声明新接口：**

在 `EcosystemState` 类的 `public` 部分：

```cpp
public:
    // ...
    /**
     * @brief [线程安全] 从空间网格中查询一个点附近的*所有*物种。
     * 这是一个"粗查询"（Broad Phase），它只检查相关单元格。
     * * @param center 查询中心点
     * @param radius 查询半径
     * @return 包含附近单元格中所有物种的列表（可能包含非精确半径内的）
     */
    std::vector<std::shared_ptr<Species>> get_nearby_species_broad(
        const Position& center, 
        double radius) const;
```

**3.2. 在 `ecosystem.cpp` 中实现新接口：**

```cpp
std::vector<std::shared_ptr<Species>> EcosystemState::get_nearby_species_broad(
    const Position& center, 
    double radius) const 
{
    std::vector<std::shared_ptr<Species>> nearby;

    // 1. 计算要检查的单元格范围
    int x_min = static_cast<int>((center.x - radius) / cell_size);
    int x_max = static_cast<int>((center.x + radius) / cell_size);
    int y_min = static_cast<int>((center.y - radius) / cell_size);
    int y_max = static_cast<int>((center.y + radius) / cell_size);

    // 2. 遍历所有相关单元格
    for (int x = x_min; x <= x_max; ++x) {
        if (x < 0 || x >= grid_width) continue; // 越界检查

        for (int y = y_min; y <= y_max; ++y) {
            if (y < 0 || y >= grid_height) continue; // 越界检查

            // 3. 将该单元格中的所有物种加入结果列表
            nearby.insert(nearby.end(), 
                          spatial_grid[x][y].begin(), 
                          spatial_grid[x][y].end());
        }
    }
    return nearby; // 返回粗查询结果
}
```

#### 阶段 4：更新“消费者” (动物和草)

**目标：** 让 `Animal` 和 `Grass` 使用新的、快速的 `get_nearby_species_broad`。

**4.1. 修改 `Animal::select_target_point` (在 `animal.cpp` 中)**

将 `decide` 阶段调用的 `select_target_point` 内部逻辑替换为：

```cpp
// (在 Animal::decide 中调用)
void Animal::select_target_point(const EcosystemState& ecosystem_state) {
    // ...
    // 1. (Broad Phase) 使用网格进行O(1)粗查询
    auto nearby_entities = ecosystem_state.get_nearby_species_broad(
        this->position, 
        this->detection_range
    );

    // 2. (Narrow Phase) 遍历粗查询结果，应用精确逻辑
    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();

    for (const auto& food_type : food_types) {
        for (const auto& entity : nearby_entities) {
            // 2a. 过滤：是食物吗？还活着吗？
            if (entity->species_name == food_type && entity->alive) {
                // 2b. 精确距离检查
                double distance = position.distance_to(entity->position);
                if (distance <= detection_range && distance < min_distance) {
                    min_distance = distance;
                    nearest_food = entity->position;
                }
            }
        }
    }
    // ... (设置 current_target 的逻辑不变) ...
}
```

**4.2. 修改 `Grass::get_competition_adjusted_growth_rate` (在 `grass.cpp` 中)**

  * **删除** `calculate_nearby_grass_density_optimized` 和 `calculate_nearby_grass_density`。
  * 直接在 `get_competition_adjusted_growth_rate` 中实现新逻辑：

<!-- end list -->

```cpp
double Grass::get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state) {
    
    // 1. (Broad Phase) 使用网格进行O(1)粗查询
    auto nearby_entities = ecosystem_state.get_nearby_species_broad(
        this->position, 
        this->competition_radius
    );

    // 2. (Narrow Phase) 遍历粗查询结果
    int nearby_grass_count = 0;
    for (const auto& entity : nearby_entities) {
        // 过滤：是草吗？是自己吗？还活着吗？
        if (entity->species_name == "grass" && 
            entity.get() != this && 
            entity->alive) 
        {
            // 精确距离检查
            if (position.distance_to(entity->position) <= competition_radius) {
                nearby_grass_count++;
            }
        }
    }
    
    // ... (计算 density 和 competition_factor 的逻辑不变) ...
}
```