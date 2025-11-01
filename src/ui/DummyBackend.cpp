#include "DummyBackend.h"

DataPacket DummyBackend::getNextFrame()
{
    DataPacket packet;

    // 添加一些草作为背景信息
    for (int i = 0; i < 50; ++i) {
        float x = static_cast<float>(QRandomGenerator::global()->generateDouble() * 200.0 - 100.0);
        float y = static_cast<float>(QRandomGenerator::global()->generateDouble() * 200.0 - 100.0);
        packet.append(DataItem(x, y, SpeciesType::Grass));
    }

    // 添加一些动物
    for (int i = 0; i < 10; ++i) {
        float x = static_cast<float>(QRandomGenerator::global()->generateDouble() * 200.0 - 100.0);
        float y = static_cast<float>(QRandomGenerator::global()->generateDouble() * 200.0 - 100.0);
        SpeciesType type;
        switch (QRandomGenerator::global()->bounded(3)) {
            case 0: type = SpeciesType::Herbivore; break;
            case 1: type = SpeciesType::Carnivore; break;
            default: type = SpeciesType::Omnivore; break;
        }
        packet.append(DataItem(x, y, type));
    }

    return packet;
}