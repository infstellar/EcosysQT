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
#include <QPushButton>
#include <QVBoxLayout>
#include <variant>

class SimulationController;  // 前向声明
// --- 新增：定义一个可以持有任何可选中生物的类型 ---
using SelectableEntity = std::variant<std::shared_ptr<RaceBase>, std::shared_ptr<ThingBase>>;


/**
 * Widget 类 - 生态系统可视化界面
 * 
 * 数据流向：
 * SimulationController (后端) -> get_data() -> std::shared_ptr<EcosystemStateData> -> Widget -> 屏幕显示
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
    // 新增：声明鼠标滚轮和鼠标移动事件处理器
    void wheelEvent(QWheelEvent *event) override;
    // --- 新增：声明鼠标按下、移动和释放事件处理器 ---
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void updateFrame();  // 每秒更新一次
    // --- 新增：控制按钮的槽函数 ---
    void onPauseResumeClicked();
    void onSpeedUpClicked();
    void onSlowDownClicked();
    void onRestartClicked();
    void onCustomSpeedClicked(); // <-- 新增：自定义速度按钮的槽函数
    void onInspectButtonClicked(); // <-- 新增：查看属性按钮的槽函数

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
     * 当前帧的数据快照，延迟复制：UI 线程请求时锁定后端并复制。
     */
    std::shared_ptr<EcosystemStateData> m_currentData;
    
    // ========== UI 资源 ==========
    
    QPixmap m_backgroundImage;
    QPixmap m_cowTexture;
    QPixmap m_bullTexture;
    QPixmap m_tigerTexture;
    QPixmap m_grassTexture;

    // --- 新增：用于高亮和选择的状态变量 ---
    std::optional<SelectableEntity> m_hoveredEntity;
    std::optional<SelectableEntity> m_selectedEntity;

    // --- 新增：UI控制按钮 ---
    QPushButton* m_inspectButton; // <-- 新增：查看属性按钮
    QPushButton* m_pauseButton;
    QPushButton* m_speedUpButton;
    QPushButton* m_slowDownButton;
    QPushButton* m_restartButton; 
    QPushButton* m_customSpeedButton; // <-- 新增：自定义速度按钮

    QTimer* m_updateTimer;
    // ========== 视图控制 ==========
    double m_zoomFactor;   // 缩放因子
    QPointF m_viewCenter;  // 视图中心点（世界坐标）
    bool m_isDragging;     // 是否正在拖动视图
    QPointF m_lastMousePos; // 上一次鼠标的位置

    // ========== 统计数据缓存 ==========
    
    int m_grassCount;
    int m_cowCount;
    int m_tigerCount;
    uint64_t m_timeStep;
    int m_currentYear;
    int m_currentDay;
    int m_currentHour;
    int m_currentMinute;
    std::string m_currentQuadrumName;
    // --- 新增：用于跟踪当前速度状态的成员 ---
    int m_currentSpeedLevel;

    // --- 新增：用于控制查看模式的状态 ---
    bool m_isInspectMode;

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
    // --- 新增：新的辅助函数 ---
    std::optional<SelectableEntity> findEntityAtScreenPos(const QPointF& screenPos);
    void drawSelectionInfo(QPainter& painter, const SelectableEntity& entity);
};

#endif // WIDGET_H