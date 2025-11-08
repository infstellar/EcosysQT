#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>
#include "logging.h"
#include "race_factory.h"
#include "thing_factory.h"
#include "species_config_provider.h"
#include "MainWindow.h" // 包含新的主窗口

int main(int argc, char *argv[])
{
    // --- 全局初始化部分 (保持不变) ---
    QDir().mkpath("logs");
    Logging::init("logs/ecosim.log");
    auto logger = spdlog::get(Logging::MAIN_LOGGER_NAME);
    if (!logger) {
        qWarning() << "Failed to acquire logger:" << QString::fromStdString(Logging::MAIN_LOGGER_NAME);
        Logging::shutdown();
        return 1;
    }
    QApplication app(argc, argv);
    
    QString currentPath = QDir::currentPath();
    qDebug() << "当前工作目录:" << currentPath;
    
    auto configExistsAt = [](const QString& dir) -> bool {
        return QFileInfo(QDir(dir).filePath("config/species.yaml")).exists();
    };
    QString exeDir = QCoreApplication::applicationDirPath();
    QString configRoot = currentPath;
    if (!configExistsAt(configRoot)) {
        if (configExistsAt(exeDir)) {
            configRoot = exeDir;
        } else {
            QString parentExeDir = QDir(exeDir).filePath("..");
            if (configExistsAt(parentExeDir)) {
                configRoot = QDir(parentExeDir).absolutePath();
            } else {
                QString parentCurrent = QDir(currentPath).filePath("..");
                if (configExistsAt(parentCurrent)) {
                    configRoot = QDir(parentCurrent).absolutePath();
                }
            }
        }
    }
    qDebug() << "YAML 配置根目录:" << configRoot;
    auto yaml_provider = std::make_shared<YamlSpeciesConfigProvider>(configRoot.toStdString());
    g_race_factory.set_config_provider(yaml_provider);
    g_thing_factory.set_config_provider(yaml_provider);
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Main] YAML provider set with root: '{}'", configRoot.toStdString());
    
    try {
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Main] Begin race registration");
        register_all_races();
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Main] Race registration completed");

        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Main] Begin thing registration");
        register_all_things();
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[Main] Thing registration completed");
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[Main] Factory registration failed: {}", e.what());
        Logging::shutdown();
        return -1;
    }
    
    // --- 核心修改：只创建和显示 MainWindow ---
    try {
        logger->info("Logging setup complete. Starting application...");
        
        // 创建并显示主窗口
        MainWindow mainWindow;
        mainWindow.show();
        
        // 进入 Qt 事件循环
        int result = app.exec();
        
        // 程序退出后清理日志
        qDebug() << "程序正常退出";
        Logging::shutdown();
        return result;
        
    } catch (const std::exception& e) {
        qDebug() << "错误:" << e.what();
        Logging::shutdown();
        return -1;
    }
}