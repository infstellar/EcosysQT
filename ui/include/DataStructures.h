#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <QList>

/**
 * ============================================
 * 前后端数据结构定义
 * ============================================
 * 
 * 这个文件定义了前后端通信的数据格式
 * 后端开发者直接使用这些定义即可
 */

// 物种类型枚举
enum class SpeciesType {
    Grass,      // 草
    Herbivore,  // 食草动物
    Carnivore,  // 食肉动物
    Omnivore    // 杂食动物
};

// 单个数据项
struct DataItem {
    float x;              // 位置 x 坐标 [-100, 100]
    float y;              // 位置 y 坐标 [-100, 100]
    SpeciesType type;     // 物种类型
    
    // 构造函数
    DataItem(float posX = 0.0f, float posY = 0.0f, SpeciesType t = SpeciesType::Grass)
        : x(posX), y(posY), type(t) {}
};

// 数据包 = 数据项列表
using DataPacket = QList<DataItem>;

#endif // DATASTRUCTURES_H