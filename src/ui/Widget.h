#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPaintEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <memory>
#include "ecosystem.h"  // 用于 EcosystemStateData
#include "utils.h"      // 用于 Position

class SimulationController;  // 前向声明

/**
 * Widget 类 - 生态系统可视化界面
 * 
 * 数据流向：
 * SimulationController (后端) -> get_data() -> EcosystemStateData -> Widget -> 屏幕显示
 * 
 * 关键变化：
 * - 不再持有 EcosystemState 的智能指针
 * - 改为持有 SimulationController 的指针
 * - 定期调用 get_data() 获取数据快照
 */
class Widget : public QWidget
{
    Q_OBJECT

public:
    /**
     * 构造函数
     * @param controller 后端模拟控制器的指针（由 main.cpp 管理生命周期）
     * @param parent Qt 父窗口指针
     */
    explicit Widget(SimulationController* controller, QWidget *parent = nullptr);
    ~Widget();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateFrame();  // 每秒更新一次

private:
    // ========== 核心数据 ==========
    
    /**
     * 后端模拟控制器的原始指针
     * 
     * 注意：这是原始指针，不是智能指针
     * 对象的生命周期由 main.cpp 管理
     * Widget 只负责使用，不负责创建或销毁
     */
    SimulationController* m_controller;
    
    /**
     * 当前帧的数据快照
     * 
     * 数据来源：m_controller->get_data()
     * 数据类型：EcosystemStateData（定义在 backend/include/utils.h）
     * 
     * 实际结构（重要！）：
     * struct EcosystemStateData {
     *     int world_width;                                              // 世界宽度
     *     int world_height;                                             // 世界高度
    *     std::map<std::string, std::vector<std::shared_ptr<RaceBase>>> race_lists;
    *     std::map<std::string, std::vector<std::shared_ptr<ThingBase>>> thing_lists;
     *     int time_step;                                                // 当前时间步
     *     Eigen::MatrixXd grass_positions_array;                        // 草的位置矩阵
    *     std::vector<std::shared_ptr<ThingBase>> alive_grass_objects;  // 存活的草对象
     * };
     * 
    * race_lists 的结构：
    * {
    *     "cow":   [shared_ptr<RaceBase>, shared_ptr<RaceBase>, ...],
    *     "tiger": [shared_ptr<RaceBase>, shared_ptr<RaceBase>, ...]
    * }
    * thing_lists 的结构：
    * {
    *     "grass": [shared_ptr<ThingBase>, shared_ptr<ThingBase>, ...]
    * }
     * 
    * RaceBase / ThingBase 对象包含：
     * - Position position        {double x, double y}
     * - double energy            当前能量值
     * - double max_energy        最大能量值
     * - int age                  年龄（时间步数）
     * - bool alive               是否存活
    * - std::string species_name 物种名称
     */
    EcosystemStateData m_currentData;
    
    // ========== UI 资源 ==========
    
    QPixmap m_backgroundImage;
    QTimer* m_updateTimer;
    
    // ========== 统计数据缓存 ==========
    
    int m_grassCount;
    int m_cowCount;
    int m_tigerCount;
    int m_timeStep;
    int m_currentYear;          // 当前年份
    int m_currentDay;           // 当前天数
    std::string m_currentQuadrumName; // 当前季度名称

    // ========== 辅助函数 ==========
    
    void updateStatistics();
    QColor getColorForName(const std::string& name) const;
    
    /**
     * 将世界坐标转换为屏幕坐标
     * @param pos Position 结构（定义在 utils.h，包含 double x, double y）
     * @return QPointF 屏幕坐标
     * 
     * 注意：参数类型是 Position，不是 PositionData
     */
    QPointF toScreenCoords(const Position& pos) const;
    
    
};

#endif // WIDGET_H