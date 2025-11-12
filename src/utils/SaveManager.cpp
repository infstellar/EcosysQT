#include "SaveManager.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <yaml-cpp/yaml.h>

// 假设 EcosystemStateData 有 toYaml() 和 fromYaml(const YAML::Node&) 方法
bool SaveManager::saveToYaml(const std::shared_ptr<EcosystemStateData>& data, const QString& filename)
{
    if (!data) {
        qWarning() << "SaveManager::saveToYaml: data is null";
        return false;
    }
    const std::string yaml = data->toYaml();
    if (yaml.empty()) {
        qWarning() << "SaveManager::saveToYaml: serialized yaml is empty";
    }

    QFile file(filename);
    // 确保目录存在
    QFileInfo fi(file);
    QDir dir = fi.dir();
    if (!dir.exists()) dir.mkpath(".");

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "无法打开存档文件写入:" << filename;
        return false;
    }
    QTextStream out(&file);
    out << QString::fromStdString(yaml);
    out.flush(); // 确保写入磁盘
    file.close();
    qDebug() << "保存存档到:" << filename << " 字节数:" << yaml.size();
    return true;
}

std::shared_ptr<EcosystemStateData> SaveManager::loadFromYaml(const QString& filename)
{
    QFile file(filename);
    if (!file.exists()) {
        qWarning() << "SaveManager::loadFromYaml: file not found:" << filename;
        return nullptr;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开存档文件读取:" << filename;
        return nullptr;
    }
    QByteArray raw = file.readAll();
    file.close();

    if (raw.trimmed().isEmpty()) {
        qDebug() << "SaveManager::loadFromYaml: file is empty:" << filename;
        return nullptr;
    }

    const QString yamlStr = QString::fromUtf8(raw);
    qDebug() << "LoadFromYaml: 文件长度:" << yamlStr.size() << " 路径:" << filename;

    std::shared_ptr<EcosystemStateData> data = std::make_shared<EcosystemStateData>();
    try {
        YAML::Node node = YAML::Load(yamlStr.toStdString());
        data->fromYaml(node);
    } catch (const std::exception& e) {
        qWarning() << "YAML 解析失败:" << e.what() << " file:" << filename;
        return nullptr;
    }
    return data;
}