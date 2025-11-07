//前端注:main.cpp里调用的：
//Widget.h，ecosystem.h，simulation.h都包含了ecosystem.h
//编译器每次编译到#include "ecosystem.h"时，
//都会把ecosystem.h的内容插入到当前位置,从而导致重复定释错误
//故ecosystem.h必须写#ifndef防止重复包含
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>
#include "Widget.h"
#include "ecosystem.h"
#include "logging.h"
#include "simulation.h"
#include "race_factory.h"
#include "thing_factory.h"
#include "species_config_provider.h"

/**
 * 主函数 - 程序入口
 * 
 * ============================================================
 * 整体架构说明
 * ============================================================
 * 
 * 本程序采用前后端分离架构：
 * 
 * 【后端】SimulationController
 *   └─ SimulationEngine (std::unique_ptr)
 *       ├─ EcosystemState (std::unique_ptr)
 *       │   ├─ RacesRegistry (移动生物的注册表)
 *       │   │   ├─ Grass 列表
 *       │   │   ├─ Cow 列表
 *       │   │   └─ Tiger 列表
 *       │   ├─ SpeciesStatistics (统计数据)
 *       │   └─ EcosystemConfig (配置)
 *       └─ 模拟线程 (30 FPS)
 * 
 * 【前端】Widget (Qt 可视化窗口)
 *   ├─ 持有 SimulationController* (原始指针)
 *   ├─ 定时器 (1 FPS，每秒调用 get_data())
 *   └─ 绘制函数 (paintEvent)
 * 
 * ============================================================
 * 数据流向
 * ============================================================
 * 
 * 后端线程 (30 FPS):
 * SimulationEngine::simulation_loop()
 *   ├─ ecosystem->update_species()      (生物行为：移动、觅食)
 *   ├─ ecosystem->handle_reproduction() (繁殖)
 *   ├─ ecosystem->cleanup_dead()        (清理死亡个体)
 *   └─ ecosystem->update_statistics()   (更新统计数据)
 * 
 * 前端线程 (1 FPS):
 * Widget::updateFrame()
 *   ├─ controller->get_data()           (获取数据快照)
 *   │   └─ 返回 EcosystemStateData
 *   │       ├─ world_width, world_height (int)
 *   │       ├─ time_step (int)
 *   │       ├─ race_lists  (map<string, vector<shared_ptr<RaceBase>>>)
 *   │       ├─ thing_lists (map<string, vector<shared_ptr<ThingBase>>>)
 *   │       └─ alive_grass_objects (vector<shared_ptr<ThingBase>>)
 *               └─ position, energy, age, alive 等
 *   ├─ updateStatistics()               (统计各物种数量)
 *   └─ update()                         (触发 paintEvent 重绘)
 * 
 * ============================================================
 * 关键数据结构（定义在 backend/include/utils.h）
 * ============================================================
 * 
 * struct EcosystemStateData {
 *     int world_width;                                              // 世界宽度
 *     int world_height;                                             // 世界高度
 *     std::map<std::string, std::vector<std::shared_ptr<RaceBase>>> race_lists;    // 动物映射
 *     std::map<std::string, std::vector<std::shared_ptr<ThingBase>>> thing_lists;  // 植物映射
 *     int time_step;                                                // 当前时间步
 *     Eigen::MatrixXd grass_positions_array;                        // 草的位置矩阵
 *     std::vector<std::shared_ptr<ThingBase>> alive_grass_objects;  // 存活的草对象
 * };
 * 
 * struct Position {
 *     double x;  // 世界坐标 X
 *     double y;  // 世界坐标 Y
 * };
 * 
 * ============================================================
 * 主函数执行流程
 * ============================================================
 * 
 * 1. 创建 QApplication（Qt 应用程序对象）
 * 2. 创建 EcosystemConfig（生态系统配置）
 * 3. 创建 SimulationController（后端模拟控制器）
 * 4. 启动模拟线程（controller->start()）
 * 5. 创建 Widget（前端可视化窗口）
 * 6. 进入 Qt 事件循环（app.exec()）
 * 7. 用户关闭窗口后，停止模拟并清理
 */
int main(int argc, char *argv[])
{
    QDir().mkpath("logs");
    Logging::init("logs/ecosim.log");
    auto logger = spdlog::get(Logging::MAIN_LOGGER_NAME);
    if (!logger) {
        qWarning() << "Failed to acquire logger:" << QString::fromStdString(Logging::MAIN_LOGGER_NAME);
        Logging::shutdown();
        return 1;
    }
    // ========== 步骤 1: 初始化 Qt 应用程序 ==========
    QApplication app(argc, argv);
    
    // 输出当前工作目录（用于调试资源文件路径）
    QString currentPath = QDir::currentPath();
    qDebug() << "当前工作目录:" << currentPath;
    
    // 设定并验证 YAML 配置根目录，然后注入配置提供者并注册物种
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
    // 物种扫描与注册加入异常捕获与诊断日志
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
    
    try {
        // ========== 步骤 2: 创建生态系统配置 ==========
        /**
         * EcosystemConfig 参数说明：
         * - world_width/world_height：世界尺寸
         * - initial_populations：通用初始种群配置，键为物种名，值为数量
         */
        EcosystemConfig config(800, 600);
        config.initial_populations = {
            {"grass", 50},
            {"cow", 10},
            {"tiger", 2},
        };
        
        // ========== 步骤 3: 创建模拟控制器 ==========
        /**
         * SimulationController 封装了整个后端模拟引擎
         * 
         * 内部结构：
         * SimulationController
         *   └─ SimulationEngine (std::unique_ptr)
         *       └─ EcosystemState (std::unique_ptr)
         *           ├─ RacesRegistry
         *           │   ├─ Cow 列表 (std::vector<std::shared_ptr<RaceBase>>)
         *           │   └─ Tiger 列表
         *           └─ m_all_things (std::vector<std::shared_ptr<ThingBase>>)
         * 
         * 所有权管理：
         * - main() 拥有 SimulationController (std::unique_ptr)
         * - SimulationController 拥有 SimulationEngine
         * - SimulationEngine 拥有 EcosystemState
         * - EcosystemState 拥有所有生物对象
         * 
         * 这种设计确保了：
         * 1. 清晰的所有权链
         * 2. 自动的资源管理（RAII）
         * 3. 线程安全的数据访问
         */
        auto controller = std::make_unique<SimulationController>(config);
        
        // ========== 步骤 4: 启动模拟 ==========
        /**
         * controller->start() 会执行以下操作：
         * 
         * 1. 初始化种群（调用 ecosystem->initialize_populations()）
         *    - 创建 100 株草（随机位置）
         *    - 创建 10 头牛（随机位置，初始能量）
         *    - 创建 2 只老虎（随机位置，初始能量）
         * 
         * 2. 启动后台模拟线程
         *    - 线程循环执行 simulation_loop()
         *    - 执行频率：30 FPS (每 33.33ms 一次)
         * 
         * 3. simulation_loop() 每帧执行：
         *    a. update_species()      - 更新所有生物的状态
         *       - 草：生长（能量增加）
         *       - 牛：移动、寻找草、吃草、消耗能量
         *       - 老虎：移动、寻找牛、捕猎、消耗能量
         *    b. handle_reproduction() - 处理繁殖
         *       - 检查能量是否足够（> 繁殖阈值）
         *       - 在附近生成新个体
         *       - 父代消耗能量
         *    c. cleanup_dead()        - 清理死亡个体
         *       - 移除 alive == false 的个体
         *       - 更新 deaths 统计
         *    d. update_statistics()   - 更新统计数据
         *       - 记录种群数量历史
         *       - 检测物种灭绝
         * 
         * 线程安全性：
         * - 模拟线程独占 EcosystemState（unique_ptr）
         * - 前端通过 get_data() 获取数据快照（拷贝）
         * - 没有共享可变状态，避免竞态条件
         */
        controller->start();
        
    qDebug() << "模拟引擎已启动";
    logger->info("Logging setup complete. Starting simulation...");
        
        // ========== 步骤 5: 创建可视化窗口 ==========
        /**
         * 传递 controller 的原始指针给 Widget
         * 
         * 关键设计决策：
         * - controller 的所有权仍由 main() 的 unique_ptr 管理
         * - Widget 只持有原始指针（SimulationController*）
         * - Widget 不负责删除 controller
         * 
         * 这确保了：
         * 1. 生命周期管理清晰
         *    - main() 结束时，controller 自动销毁
         *    - Widget 销毁时不会尝试删除 controller
         * 2. 调用顺序正确
         *    - app.exec() 退出后，先销毁 mainWidget
         *    - 然后 main() 函数结束，销毁 controller
         *    - controller 销毁时会停止模拟线程
         */
        Widget mainWidget(controller.get());
        mainWidget.setWindowTitle("生态系统模拟");
        mainWidget.resize(800, 600);
        mainWidget.show();
        
        // ========== 步骤 6: 输出初始状态 ==========
        /**
         * 获取第一帧数据快照并输出统计信息
         * 
         * get_data() 返回的数据结构：
         * EcosystemStateData {
         *     int world_width;                                              // 世界宽度
         *     int world_height;                                             // 世界高度
         *     std::map<std::string, std::vector<std::shared_ptr<RaceBase>>> race_lists;    // 动物 map
         *     std::map<std::string, std::vector<std::shared_ptr<ThingBase>>> thing_lists;  // 植物 map
         *     int time_step;                                                // 当前时间步
         *     Eigen::MatrixXd grass_positions_array;                        // 草的位置矩阵
         *     std::vector<std::shared_ptr<ThingBase>> alive_grass_objects;  // 存活的草对象
         * };
         * 
         * race_lists / thing_lists 均为 map 类型（见 utils.h）
         */
        EcosystemStateData initialData = controller->get_data();
        qDebug() << "生态系统初始化完成";
        qDebug() << "世界大小:" << initialData.world_width << "x" << initialData.world_height;
        qDebug() << "时间步:" << initialData.time_step;
        
        // 统计初始种群（遍历 map）
        /**
         * race_lists / thing_lists 的实际结构：
         * std::map<std::string, std::vector<std::shared_ptr<RaceBase/ThingBase>>>
         * 
         * map 的内容（注意：后端使用小写键名）：
         * race_lists 示例：{"cow": [...], "tiger": [...]}；thing_lists 示例：{"grass": [...]}。
         * 
         * C++17 结构化绑定语法：
         * for (const auto& [key, value] : map) {...}
         * 
         * 等价于传统写法：
         * for (const auto& pair : map) {
         *     const std::string& species_name = pair.first;   // map 的 key（小写）
         *     const std::vector<std::shared_ptr<RaceBase/ThingBase>>& individuals = pair.second;  // map 的 value
         * }
         * 
         * species_name 的可能值取决于对应 map：
         * - race_lists: "cow"、"tiger"（小写）
         * - thing_lists: "grass" 等植物名称（小写）
         * 
         * individuals 是该物种的所有个体（智能指针列表）
         */
        qDebug() << "Races:";
        for (const auto& [species_name, individuals] : initialData.race_lists) {
            int alive_count = 0;
            for (const auto& ind : individuals) {
                if (ind && ind->alive) {
                    alive_count++;
                }
            }

            qDebug() << "  " << QString::fromStdString(species_name)
                     << ":" << alive_count;
        }

        qDebug() << "Things:";
        for (const auto& [species_name, individuals] : initialData.thing_lists) {
            int alive_count = 0;
            for (const auto& ind : individuals) {
                if (ind && ind->alive) {
                    alive_count++;
                }
            }

            qDebug() << "  " << QString::fromStdString(species_name)
                     << ":" << alive_count;
        }
        
        // ========== 步骤 7: 进入 Qt 事件循环 ==========
        /**
         * app.exec() 会阻塞在这里，进入事件循环
         * 
         * 此时程序同时运行：
         * 
         * 1. Qt 主线程（UI 线程）：
         *    - 处理 UI 事件（鼠标、键盘、窗口）
         *    - 处理定时器事件（Widget 的 updateTimer，每秒触发）
         *    - 调用 paintEvent 进行绘制
         * 
         * 2. 模拟线程（后台线程）：
         *    - 以 30 FPS 的频率更新生态系统状态
         *    - 独立于 UI 线程运行
         * 
         * 线程通信：
         * - Widget 每秒调用 controller->get_data()
         * - get_data() 返回数据快照（拷贝）
         * - 没有共享可变数据，线程安全
         * 
         * 退出条件：
         * - 用户关闭窗口
         * - app.quit() 被调用
         * - 返回退出代码（通常为 0）
         */
        int result = app.exec();
        
        // ========== 步骤 8: 清理 ==========
        /**
         * 用户关闭窗口后到达这里
         * 
         * 清理顺序：
         * 1. mainWidget 已经被销毁（离开作用域）
         * 2. 调用 controller->stop() 停止模拟线程
         *    - 设置 running_ = false
         *    - 等待线程退出（join）
         * 3. controller 的 unique_ptr 自动销毁
         *    - 依次销毁：controller → engine → ecosystem
         *    - 自动释放所有资源
         * 
         * RAII (Resource Acquisition Is Initialization) 的优势：
         * - 不需要手动 delete
         * - 保证资源被正确释放
         * - 异常安全（即使抛出异常也会清理）
         */
        qDebug() << "正在停止模拟...";
        controller->stop();
        
        qDebug() << "程序正常退出";
        Logging::shutdown();
        return result;
        
    } catch (const std::exception& e) {
        /**
         * 异常处理：捕获所有标准异常
         * 
         * 可能的异常来源：
         * - YAML 配置文件解析失败
         * - 内存分配失败
         * - 文件打开失败
         * - 线程创建失败
         * 
         * 错误处理：
         * - 输出错误信息到调试控制台
         * - 返回 -1 表示异常退出
         * - unique_ptr 会自动清理已分配的资源
         */
        qDebug() << "错误:" << e.what();
        Logging::shutdown();
        return -1;
    }
}