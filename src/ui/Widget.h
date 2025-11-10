#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPaintEvent>
#include <QTimer>
#include <memory>
#include <optional>
#include <variant>
#include "ecosystem.h"  // 用于 EcosystemStateData
#include "utils.h"      // 用于 Position
#include <QPushButton>
#include <QVBoxLayout>

// 前向声明
class SimulationController;
class CameraController;
class SimulationRenderer;
class RaceBase;
class ThingBase;

// --- 定义一个可以持有任何可选中生物的类型 ---
using SelectableEntity = std::variant<std::shared_ptr<RaceBase>, std::shared_ptr<ThingBase>>;


/**
 * Widget 类 - 生态系统可视化界面 (重构后)
 * 
 * 职责：
 * - 作为视图的根容器。
 * - 管理UI控件（按钮）的创建、布局和事件。
 * - 协调后端(SimulationController)、渲染器(SimulationRenderer)和相机(CameraController)。
 * - 处理用户输入事件，并将其分发给对应的子系统。
 */
class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(SimulationController* controller, QWidget *parent = nullptr);
    ~Widget();

signals:
    void exitToStartScreen();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void updateFrame();
    void onPauseResumeClicked();
    void onSpeedUpClicked();
    void onSlowDownClicked();
    void onRestartClicked();
    void onCustomSpeedClicked();
    void onInspectButtonClicked();
    void onExitToStartScreenClicked();

private:
    // ========== 核心数据和子系统 ==========
    SimulationController* m_controller;
    std::shared_ptr<EcosystemStateData> m_currentData;
    std::unique_ptr<CameraController> m_cameraController;
    std::unique_ptr<SimulationRenderer> m_renderer;

    // ========== UI 控件 ==========
    QPushButton* m_exitButton;
    QPushButton* m_inspectButton;
    QPushButton* m_pauseButton;
    QPushButton* m_speedUpButton;
    QPushButton* m_slowDownButton;
    QPushButton* m_restartButton; 
    QPushButton* m_customSpeedButton;

    QTimer* m_updateTimer;

    // ========== UI 状态 ==========
    bool m_isDragging;     // 是否正在拖动视图
    bool m_isInspectMode;  // 是否处于查看模式
    int m_currentSpeedLevel;
    std::optional<SelectableEntity> m_hoveredEntity;
    std::optional<SelectableEntity> m_selectedEntity;

    // ========== 统计数据缓存 ==========
    int m_grassCount;
    int m_cowCount;
    int m_tigerCount;
    uint64_t m_timeStep;
    int m_currentYear;
    int m_currentDay;
    std::string m_currentQuadrumName;
    int m_currentHour;
    int m_currentMinute;
    double m_current_tps;

    // ========== 辅助函数 ==========
    void updateStatistics();
    std::optional<SelectableEntity> findEntityAtScreenPos(const QPointF& screenPos);

    // --- 让子系统可以访问私有数据 ---
    friend class SimulationRenderer;
};

#endif // WIDGET_H