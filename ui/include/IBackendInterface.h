#ifndef IBACKENDINTERFACE_H
#define IBACKENDINTERFACE_H

#include "DataStructures.h"

/**
 * ============================================
 * 后端接口说明文档
 * ============================================
 * 
 * 后端开发者只需要实现 IBackendInterface 接口
 * 
 * 接口说明:
 * 1. getNextFrame() - 返回下一帧的数据
 *    返回值: DataPacket (QList<DataItem>)
 *    
 * 2. DataItem 结构:
 *    - float x: 位置 x 坐标 (范围: -100.0 ~ 100.0)
 *    - float y: 位置 y 坐标 (范围: -100.0 ~ 100.0)
 *    - SpeciesType type: 物种类型
 * 
 * 3. SpeciesType 枚举:
 *    - SpeciesType::Grass      (草)
 *    - SpeciesType::Herbivore  (食草动物)
 *    - SpeciesType::Carnivore  (食肉动物)
 *    - SpeciesType::Omnivore   (杂食动物)
 * 
 * 使用示例:
 * 
 * class MyBackend : public IBackendInterface {
 * public:
 *     DataPacket getNextFrame() override {
 *         DataPacket packet;
 *         packet.append(DataItem(10.0f, 20.0f, SpeciesType::Herbivore));
 *         packet.append(DataItem(-30.0f, 50.0f, SpeciesType::Carnivore));
 *         return packet;
 *     }
 * };
 * 
 * ============================================
 */

class IBackendInterface
{
public:
    virtual ~IBackendInterface() = default;
    
    /**
     * @brief 获取下一帧的数据
     * @return DataPacket 包含所有生物位置和类型的列表
     * 
     * 注意事项:
     * 1. 此函数每秒会被调用一次 (由前端控制)
     * 2. 坐标范围: x, y ∈ [-100, 100]
     * 3. 返回的是完整的当前状态快照,不是增量更新
     */
    virtual DataPacket getNextFrame() = 0;
};

#endif // IBACKENDINTERFACE_H