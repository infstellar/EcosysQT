# 后端开发者接口说明

## 快速开始

### 1. 实现接口

创建您的后端类，继承 `IBackendInterface`:

```cpp
// RealBackend.h
#include "IBackendInterface.h"

class RealBackend : public IBackendInterface {
public:
    DataPacket getNextFrame() override;
private:
    // 您的数据成员
};
```

### 2. 实现 getNextFrame()

```cpp
// RealBackend.cpp
DataPacket RealBackend::getNextFrame() {
    DataPacket packet;
    
    // 您的生态模拟逻辑
    // 例如:
    packet.append(DataItem(10.0f, 20.0f, SpeciesType::Herbivore));
    
    return packet;
}
```

### 3. 在 main.cpp 中替换后端

```cpp
// main.cpp
#include "RealBackend.h"

int main(int argc, char *argv[]) {
    RealBackend backend;  // 使用您的后端
    Widget w(&backend);
    // ...
}
```

### 4. 更新 CMakeLists.txt

```cmake
set(PROJECT_SOURCES
    # ...
    RealBackend.cpp
    RealBackend.h
)
```

## 接口规范

### 坐标系统
- X 轴范围: [-100, 100]
- Y 轴范围: [-100, 100]
- 原点 (0, 0) 在屏幕中心

### 物种类型
- `SpeciesType::Grass` - 草 (显示为背景)
- `SpeciesType::Herbivore` - 食草动物 (浅蓝色)
- `SpeciesType::Carnivore` - 食肉动物 (红色)
- `SpeciesType::Omnivore` - 杂食动物 (橙色)

### 调用频率
- `getNextFrame()` 每秒被调用一次

### 返回值
- 返回完整的状态快照，不是增量更新
- 可以包含任意数量的生物

## 参考实现

查看 `TestBackend.cpp` 了解简单的实现示例。