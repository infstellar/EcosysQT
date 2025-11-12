#pragma once
#include <QString>
#include <memory>
#include "ecosystem.h" // EcosystemStateData

namespace SaveManager {
    bool saveToYaml(const std::shared_ptr<EcosystemStateData>& data, const QString& filename);
    std::shared_ptr<EcosystemStateData> loadFromYaml(const QString& filename);
}