/*
 * EcosysQT - Headless Mode Entry Point
 * * 为性能测试构建一个无 GUI、无日志的控制台版本。
 * 使用 QCoreApplication 来驱动模拟和 TPS 报告定时器。
 * * 编译: cmake .. -DECOSIM_HEADLESS=ON
 */

#include <memory>
#include <vector>
#include <iostream>
#include <stdio.h> // 使用 printf 实现高性能单行刷新
#include <string>

// --- 核心 Includes (非 GUI) ---
#include "simulation.h"
#include "ecosystem.h"
#include "map_config_loader.h"
#include "race_factory.h"
#include "thing_factory.h"
#include "species_config_provider.h"
#include "utils.h" // 为了 EcosystemStateData
#include "logging.h" // 为了 MAIN_LOGGER_NAME

// --- Qt Core Includes (无 GUI) ---
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QString>

// --- 新增 includes: 注册空日志记录器 ---
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

/**
 * @brief 查找配置根目录 (从 main.cpp 复制而来)
 */
QString findConfigRoot() {
    QString currentPath = QDir::currentPath();
    qDebug() << "[Headless] 当前工作目录:" << currentPath;
    
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
    qDebug() << "[Headless] YAML 配置根目录:" << configRoot;
    return configRoot;
}

// --- Headless Main 入口 ---
int main(int argc, char *argv[])
{
    // 1. 创建 QCoreApplication (无 GUI)
    QCoreApplication app(argc, argv);
    
    qDebug() << "[Headless] 模式已启用。正在初始化模拟...";
    qDebug() << "[Headless] 正在注册 'ecosim' null logger (禁用日志输出)...";
    // 注册一个空日志记录器，避免后端获取 logger 失败导致崩溃
    try {
        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        auto null_logger = std::make_shared<spdlog::logger>(Logging::MAIN_LOGGER_NAME, null_sink);
        spdlog::register_logger(null_logger);
        spdlog::set_default_logger(null_logger);
        spdlog::set_level(spdlog::level::off);
    } catch (const std::exception& e) {
        qCritical() << "[Headless] 注册 null logger 失败:" << e.what();
        return -1;
    }

    // 2. 加载配置并注册工厂
    QString configRoot;
    std::shared_ptr<YamlSpeciesConfigProvider> yaml_provider;
    try {
        configRoot = findConfigRoot();
        yaml_provider = std::make_shared<YamlSpeciesConfigProvider>(configRoot.toStdString());
        g_race_factory.set_config_provider(yaml_provider);
        g_thing_factory.set_config_provider(yaml_provider);

        qDebug() << "[Headless] 正在注册 races...";
        register_all_races();
        qDebug() << "[Headless] 正在注册 things...";
        register_all_things();
    } catch (const std::exception& e) {
        qCritical() << "[Headless] 工厂或配置初始化失败:" << e.what();
        return -1;
    }

    // 3. 创建模拟控制器
    qDebug() << "[Headless] 正在加载地图配置...";
    EcosystemConfig config = load_map_config_from_yaml("config/map_config.yaml");
    qDebug() << "[Headless] 地图尺寸:" << config.world_width << "x" << config.world_height;
    auto controller = std::make_unique<SimulationController>(config);

    // 4. 设置 TPS 报告定时器
    QTimer tpsTimer;
    tpsTimer.setInterval(1000); // 每秒报告一次 TPS

    QObject::connect(&tpsTimer, &QTimer::timeout, [&]() {
        if (controller) {
            auto data = controller->get_data();
            if (data) {
                // 使用 printf 和 \r 来创建单行刷新的状态显示
                // \033[K 用于清除行尾的残留字符
                printf("\r\033[K[运行中] 年: %-4d  天: %-5d | 模拟 TPS: %.2f", 
                       data->current_year, 
                       data->current_day, 
                       data->current_tps);
                fflush(stdout); // 确保立即输出到控制台
            }
        }
    });

    // 5. 启动模拟
    qDebug() << "\n[Headless] 正在启动模拟线程 (目标 FPS 设为 2000 以压榨性能)...";
    // 设置一个非常高的目标 FPS (例如 2000)，以确保模拟线程不会主动休眠
    controller->set_target_fps(2000); 
    controller->start();
    tpsTimer.start();

    // 6. 运行核心事件循环
    qDebug() << "[Headless] 模拟正在运行。按 Ctrl+C 停止。";
    int result = app.exec();

    // 7. 清理 (在 Ctrl+C 触发 app.quit() 后)
    printf("\n[Headless] 正在关闭模拟线程...\n");
    controller->stop();
    tpsTimer.stop();
    qDebug() << "[Headless] 关闭完成。";
    return result;
}