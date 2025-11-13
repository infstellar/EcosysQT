#include "Widget.h"
#include "simulation.h"
#include "race_base.h"
#include "thing_base.h"
#include "animal.h"
#include "CameraController.h"
#include "SimulationRenderer.h"
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <cmath>
#include "map_config_loader.h"

/**
 * 构造函数实现
 * 
 * @param controller 后端模拟控制器指针（由 main.cpp 传入）
 * 
 * 数据流程说明：
 * SimulationController (后端)
 *   └─ SimulationEngine (unique_ptr)
 *       └─ EcosystemState (unique_ptr)
 *           └─ RacesRegistry (移动生物数据)
 * 
 * Widget 通过 controller->get_data() 获取数据快照（只读）
 */
Widget::Widget(SimulationController* controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_currentData(std::make_shared<EcosystemStateData>())
    , m_updateTimer(new QTimer(this))
    , m_isDragging(false)
    , m_isInspectMode(false)
    , m_isGridInspectMode(false)
    , m_showGrid(false)
#ifdef ECOSIM_ENABLE_UI_DEBUG
    , m_showHistory(false)
#endif
    , m_currentSpeedLevel(2)
    // --- 初始化统计数据缓存 ---
    , m_timeStep(0)
    , m_currentYear(1)
    , m_currentDay(1)
    , m_currentQuadrumName("Aprimay")
    , m_currentHour(0)
    , m_currentMinute(0)
    , m_current_tps(0.0)
{
    // --- 初始化子系统 ---
    if (m_controller) {
        auto initialData = m_controller->get_data();
        if (initialData) {
            m_currentData = initialData;
            // 创建相机控制器，并传入世界尺寸
            m_cameraController = std::make_unique<CameraController>(initialData->world_width, initialData->world_height);
        }
    }
    // 如果相机控制器没有被成功创建（例如后端数据获取失败），创建一个默认的
    if (!m_cameraController) {
        m_cameraController = std::make_unique<CameraController>(8000, 6000); // 使用一个默认尺寸
    }
    // 创建渲染器
    m_renderer = std::make_unique<SimulationRenderer>(this);

    // --- 创建所有控制按钮 ---
    m_exitButton = new QPushButton("Exit Simulation", this);
    m_inspectButton = new QPushButton("Inspect", this);
    m_restartButton = new QPushButton("Restart", this);
    m_customSpeedButton = new QPushButton("Custom Speed", this);
    m_pauseButton = new QPushButton("Pause", this);
    m_slowDownButton = new QPushButton("Slow Down (-)", this);
    m_speedUpButton = new QPushButton("Speed Up (+)", this);
#ifdef ECOSIM_ENABLE_UI_DEBUG
    m_historyButton = new QPushButton("Show History (OFF)", this);
#endif
    // 新增：显示/隐藏网格按钮（右下角）
    m_toggleGridButton = new QPushButton("Show Grid", this);
    m_toggleHpBarButton = new QPushButton("Show Health Bars", this);
    m_toggleGridInspectButton = new QPushButton("Inspect Tiles", this);
    

    // --- 设置按钮样式 ---
    QString buttonStyle = "QPushButton { background-color: rgba(0, 0, 0, 180); color: white; border: 1px solid white; padding: 5px; border-radius: 3px; min-width: 80px; } QPushButton:hover { background-color: rgba(255, 255, 255, 50); } QPushButton:pressed { background-color: rgba(0, 0, 0, 220); }";
    m_exitButton->setStyleSheet(buttonStyle);
    m_inspectButton->setStyleSheet(buttonStyle);
    m_restartButton->setStyleSheet(buttonStyle);
    m_customSpeedButton->setStyleSheet(buttonStyle);
    m_pauseButton->setStyleSheet(buttonStyle);
    m_slowDownButton->setStyleSheet(buttonStyle);
    m_speedUpButton->setStyleSheet(buttonStyle);
#ifdef ECOSIM_ENABLE_UI_DEBUG
    m_historyButton->setStyleSheet(buttonStyle);
#endif
    m_toggleGridButton->setStyleSheet(buttonStyle);
    m_toggleHpBarButton->setStyleSheet(buttonStyle);
    m_toggleGridInspectButton->setStyleSheet(buttonStyle);

    // --- 按钮布局 (保持不变) ---
    QHBoxLayout* topRowLayout = new QHBoxLayout();
    topRowLayout->addWidget(m_inspectButton);
#ifdef ECOSIM_ENABLE_UI_DEBUG
    topRowLayout->addWidget(m_historyButton);
    m_historyButton->setVisible(false);
#endif
    topRowLayout->addWidget(m_restartButton);
    topRowLayout->addWidget(m_exitButton);
    QHBoxLayout* bottomRowLayout = new QHBoxLayout();
    bottomRowLayout->addWidget(m_slowDownButton);
    bottomRowLayout->addWidget(m_pauseButton);
    bottomRowLayout->addWidget(m_speedUpButton);
    bottomRowLayout->addWidget(m_toggleGridButton);
    bottomRowLayout->addWidget(m_toggleHpBarButton);
    bottomRowLayout->addWidget(m_toggleGridInspectButton);
    QHBoxLayout* customSpeedLayout = new QHBoxLayout();
    customSpeedLayout->addStretch();
    customSpeedLayout->addWidget(m_customSpeedButton);
    customSpeedLayout->addStretch();
    QVBoxLayout* controlsLayout = new QVBoxLayout();
    controlsLayout->addLayout(topRowLayout);
    controlsLayout->addLayout(bottomRowLayout);
    controlsLayout->addLayout(customSpeedLayout);
    QHBoxLayout* hLayout = new QHBoxLayout();
    hLayout->addStretch();
    hLayout->addLayout(controlsLayout);
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addStretch();
    mainLayout->addLayout(hLayout);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    setLayout(mainLayout);

    // --- 连接信号和槽 ---
    connect(m_exitButton, &QPushButton::clicked, this, &Widget::onExitToStartScreenClicked);
    connect(m_inspectButton, &QPushButton::clicked, this, &Widget::onInspectButtonClicked);
    connect(m_restartButton, &QPushButton::clicked, this, &Widget::onRestartClicked);
    connect(m_customSpeedButton, &QPushButton::clicked, this, &Widget::onCustomSpeedClicked);
    connect(m_pauseButton, &QPushButton::clicked, this, &Widget::onPauseResumeClicked);
    connect(m_slowDownButton, &QPushButton::clicked, this, &Widget::onSlowDownClicked);
    connect(m_speedUpButton, &QPushButton::clicked, this, &Widget::onSpeedUpClicked);
#ifdef ECOSIM_ENABLE_UI_DEBUG
    connect(m_historyButton, &QPushButton::clicked, this, &Widget::onToggleHistoryClicked);
#endif
    connect(m_toggleGridButton, &QPushButton::clicked, this, &Widget::onToggleGridClicked);
    connect(m_updateTimer, &QTimer::timeout, this, &Widget::updateFrame);
    connect(m_toggleGridInspectButton, &QPushButton::clicked, this, &Widget::onToggleGridInspectClicked);
    connect(m_toggleHpBarButton, &QPushButton::clicked, this, [this]() {
        m_showHpBar = !m_showHpBar;
        m_toggleHpBarButton->setText(m_showHpBar ? "Hide Health Bars" : "Show Health Bars");
        update();
    });
    
    m_updateTimer->start(16); // 约 60 FPS 的UI刷新率
    
    if (m_controller) {
        m_controller->set_target_fps(30);
        // 初始化时获取一次数据，确保相机和统计数据被正确设置
        updateFrame();
    }

    // --- 新增：设置相机初始视图为世界中心的 1/4（zoom = 4） ---
    if (m_cameraController && m_currentData) {
        // 初始缩放因子：显示地图的 1/4
        m_cameraController->setZoomFactor(4.0);
        m_cameraController->setViewCenter(QPointF(m_currentData->world_width / 2.0, m_currentData->world_height / 2.0));
        // 在构造时尝试 clamp（若窗口尺寸可用）
        m_cameraController->clampToBounds(size());
    }
}

Widget::~Widget()
{
    // unique_ptr 会在这里自动释放 m_cameraController 和 m_renderer
}

/**
 * 定时更新函数
 * 
 * 执行流程：
 * 1. 调用 controller->get_data() 获取最新的数据快照
 * 2. 更新统计数据缓存
 * 3. 触发重绘
 */
void Widget::updateFrame()
{
    if (!m_controller) {
        qDebug() << "Error: controller pointer is null";
        return;
    }
    
    auto newData = m_controller->get_data();
    if (newData) {
        m_currentData = newData;
    }
    
    updateStatistics();
    update(); // 请求重绘
}

/**
 * 更新统计数据
 * 
 * 从 m_currentData 中提取各物种的数量
 */
void Widget::updateStatistics()
{
    if (!m_controller) return;
    const auto data = m_currentData;
    if (!data) return;
    
    m_speciesCounts.clear();
    
    m_timeStep = data->time_step;
    m_currentYear = data->current_year;
    m_currentDay = data->current_day;
    m_currentQuadrumName = data->current_quadrum_name;
    m_currentHour = data->current_hour;
    m_currentMinute = data->current_minute;
    m_current_tps = data->current_tps;

    for (const auto& [name, individuals] : data->race_lists) {
        m_speciesCounts[name] = static_cast<int>(individuals.size());
    }
    for (const auto& [name, individuals] : data->thing_lists) {
        m_speciesCounts[name] = static_cast<int>(individuals.size());
    }
}

/**
 * 绘图事件处理函数
 * 
 * 将所有绘制工作委托给 SimulationRenderer
 */
void Widget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    m_renderer->render(painter,
                       m_currentData,
                       *m_cameraController,
                       m_hoveredEntity,
                       m_selectedEntity,
                       m_isInspectMode,
                       m_isGridInspectMode,
                       m_hoveredGridCoords);
}

/**
 * 鼠标滚轮事件处理函数
 * 
 * 将事件委托给 CameraController
 */
void Widget::wheelEvent(QWheelEvent *event)
{
    m_cameraController->handleWheelEvent(event, size());
    update(); // 请求重绘
    event->accept();
}

/**
 * 鼠标按下事件处理函数
 * 
 * - 左键：处理实体选择
 * - 中键：开始视图拖动
 */
void Widget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isInspectMode) {
            if (m_hoveredEntity.has_value()) {
                m_selectedEntity = m_hoveredEntity;
            } else {
                m_selectedEntity.reset();
            }
            update();
            event->accept();
        } else {
            event->ignore();
        }
    } else if (event->button() == Qt::MiddleButton) {
        m_isDragging = true;
        m_cameraController->setLastMousePos(event->localPos());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        event->ignore();
    }
}

/**
 * 鼠标移动事件处理函数
 * 
 * - 如果正在拖动：将事件委托给 CameraController 进行平移
 * - 如果处于查看模式：查找悬停的实体
 */
void Widget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        m_cameraController->handleMouseMoveEventForPan(event, size());
        update();
        event->accept();
        return;
    }

    bool needsUpdate = false;

    if (m_isInspectMode) {
        if (m_hoveredGridCoords.has_value()) {
            m_hoveredGridCoords.reset();
            needsUpdate = true;
        }

        auto previouslyHovered = m_hoveredEntity;
        m_hoveredEntity = findEntityAtScreenPos(event->localPos());

        if (previouslyHovered.has_value() != m_hoveredEntity.has_value() ||
            (previouslyHovered.has_value() && m_hoveredEntity.has_value() &&
             std::visit([](auto&& arg1){ return static_cast<const void*>(arg1.get()); }, previouslyHovered.value()) !=
             std::visit([](auto&& arg2){ return static_cast<const void*>(arg2.get()); }, m_hoveredEntity.value()))) {
            needsUpdate = true;
        }
    } else if (m_isGridInspectMode) {
        if (m_hoveredEntity.has_value()) {
            m_hoveredEntity.reset();
            needsUpdate = true;
        }

        if (m_currentData) {
            QPointF worldPos = m_cameraController->toWorldCoords(event->localPos(), size());
            QPoint gridCoords(static_cast<int>(std::floor(worldPos.x())),
                              static_cast<int>(std::floor(worldPos.y())));

            if (gridCoords.x() >= 0 && gridCoords.x() < m_currentData->world_width &&
                gridCoords.y() >= 0 && gridCoords.y() < m_currentData->world_height) {
                if (!m_hoveredGridCoords.has_value() || m_hoveredGridCoords.value() != gridCoords) {
                    m_hoveredGridCoords = gridCoords;
                    needsUpdate = true;
                }
            } else if (m_hoveredGridCoords.has_value()) {
                m_hoveredGridCoords.reset();
                needsUpdate = true;
            }
        }
    } else {
        if (m_hoveredEntity.has_value()) {
            m_hoveredEntity.reset();
            needsUpdate = true;
        }
        if (m_hoveredGridCoords.has_value()) {
            m_hoveredGridCoords.reset();
            needsUpdate = true;
        }
    }

    if (needsUpdate) {
        update();
    }

    event->ignore();
}

/**
 * 鼠标释放事件处理函数
 * 
 * - 中键：结束视图拖动
 */
void Widget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        event->ignore();
    }
}

// --- 按钮的槽函数 ---

void Widget::onRestartClicked()
{
    if (!m_controller) return;
    qDebug() << "Requesting simulation restart...";
    EcosystemConfig newConfig = load_map_config_from_yaml("config/map_config.yaml");
    qDebug() << "Creating new config: world size " << newConfig.world_width << "x" << newConfig.world_height;
    for (const auto& pair : newConfig.initial_populations) {
        qDebug() << " - Initial population: " << QString::fromStdString(pair.first) << ", count: " << pair.second;
    }
    m_controller->reset(newConfig);
    // 重置相机
    m_cameraController->setZoomFactor(1.0);
    m_cameraController->setViewCenter(QPointF(newConfig.world_width / 2.0, newConfig.world_height / 2.0));
    m_currentSpeedLevel = 4;
    m_pauseButton->setText(m_controller->is_paused() ? "Resume" : "Pause");
    updateFrame();
}

void Widget::onPauseResumeClicked()
{
    if (!m_controller) return;
    if (m_controller->is_paused()) {
        m_controller->resume();
        m_pauseButton->setText("Pause");
    } else {
        m_controller->pause();
        m_pauseButton->setText("Resume");
    }
}

void Widget::onSpeedUpClicked()
{
    if (!m_controller) return;
    m_currentSpeedLevel++;
    const std::map<int, int> speedMap = {
        {0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, 
        {5, 60}, {6, 70}, {7, 80}, {8, 90}, {9, 100}
    };
    if (m_currentSpeedLevel > 9) m_currentSpeedLevel = 9;
    auto it = speedMap.find(m_currentSpeedLevel);
    if (it != speedMap.end()) {
        m_controller->set_target_fps(it->second);
        qDebug() << "Speed level:" << m_currentSpeedLevel << ", FPS:" << it->second;
    }
}

void Widget::onSlowDownClicked()
{
    if (!m_controller) return;
    m_currentSpeedLevel--;
    const std::map<int, int> speedMap = {
        {0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, 
        {5, 60}, {6, 70}, {7, 80}, {8, 90}, {9, 100}
    };
    if (m_currentSpeedLevel < 0) m_currentSpeedLevel = 0;
    auto it = speedMap.find(m_currentSpeedLevel);
    if (it != speedMap.end()) {
        m_controller->set_target_fps(it->second);
        qDebug() << "Speed level:" << m_currentSpeedLevel << ", FPS:" << it->second;
    }
}

// 新增：显示/隐藏网格
void Widget::onToggleGridClicked()
{
    m_showGrid = !m_showGrid;
    if (m_toggleGridButton) {
        m_toggleGridButton->setText(m_showGrid ? "Hide Grid" : "Show Grid");
    }
    update();
}

void Widget::onCustomSpeedClicked()
{
    if (!m_controller) return;
    bool ok;
    int newFps = QInputDialog::getInt(this, "Set Simulation Speed", "Enter target FPS (1-2000):", 30, 1, 2000, 1, &ok);
    if (ok) {
        m_controller->set_target_fps(newFps);
        qDebug() << "Custom speed set to:" << newFps << "FPS";
    }
}

void Widget::onInspectButtonClicked()
{
    m_isInspectMode = !m_isInspectMode;
#ifdef ECOSIM_ENABLE_UI_DEBUG
    m_historyButton->setVisible(m_isInspectMode);
    if (!m_isInspectMode) {
        m_showHistory = false;
        m_historyButton->setText("Show History (OFF)");
    }
#endif
    if (m_isInspectMode) {
        if (m_isGridInspectMode) {
            m_isGridInspectMode = false;
            if (m_toggleGridInspectButton) {
                m_toggleGridInspectButton->setText("Inspect Tiles");
            }
            m_hoveredGridCoords.reset();
        }
        m_inspectButton->setText("Exit Inspect");
        m_inspectButton->setStyleSheet("QPushButton { background-color: #007ACC; color: white; border: 1px solid #005A9E; padding: 5px; border-radius: 3px; min-width: 80px; }");
    } else {
        m_inspectButton->setText("Inspect");
        QString buttonStyle = "QPushButton { background-color: rgba(0, 0, 0, 180); color: white; border: 1px solid white; padding: 5px; border-radius: 3px; min-width: 80px; } QPushButton:hover { background-color: rgba(255, 255, 255, 50); } QPushButton:pressed { background-color: rgba(0, 0, 0, 220); }";
        m_inspectButton->setStyleSheet(buttonStyle);
        m_hoveredEntity.reset();
        m_selectedEntity.reset();
        update();
    }
}

void Widget::onToggleGridInspectClicked()
{
    m_isGridInspectMode = !m_isGridInspectMode;

    if (m_isGridInspectMode) {
        if (m_isInspectMode) {
            m_isInspectMode = false;
            m_inspectButton->setText("Inspect");
            QString buttonStyle = "QPushButton { background-color: rgba(0, 0, 0, 180); color: white; border: 1px solid white; padding: 5px; border-radius: 3px; min-width: 80px; } QPushButton:hover { background-color: rgba(255, 255, 255, 50); } QPushButton:pressed { background-color: rgba(0, 0, 0, 220); }";
            m_inspectButton->setStyleSheet(buttonStyle);
            m_hoveredEntity.reset();
            m_selectedEntity.reset();
#ifdef ECOSIM_ENABLE_UI_DEBUG
            if (m_historyButton) {
                m_historyButton->setVisible(false);
                m_showHistory = false;
                m_historyButton->setText("Show History (OFF)");
            }
#endif
        }
        m_hoveredGridCoords.reset();
        if (m_toggleGridInspectButton) {
            m_toggleGridInspectButton->setText("Exit Tile Inspect");
        }
    } else {
        if (m_toggleGridInspectButton) {
            m_toggleGridInspectButton->setText("Inspect Tiles");
        }
        m_hoveredGridCoords.reset();
    }

    update();
}

#ifdef ECOSIM_ENABLE_UI_DEBUG
void Widget::onToggleHistoryClicked()
{
    m_showHistory = !m_showHistory;
    m_historyButton->setText(m_showHistory ? "Show History (ON)" : "Show History (OFF)");
    update();
}
#endif

void Widget::onExitToStartScreenClicked()
{
    emit exitToStartScreen();
}

// --- 辅助函数 ---

std::optional<SelectableEntity> Widget::findEntityAtScreenPos(const QPointF& screenPos)
{
    const auto data = m_currentData;
    if (!data) return std::nullopt;

    double closestDistSq = 30.0 * 30.0; // 30像素的点击半径
    std::optional<SelectableEntity> foundEntity = std::nullopt;

    auto checkList = [&](const auto& list) {
        for (const auto& individual : list) {
            if (!individual || !individual->alive) continue;
            // 使用相机控制器进行坐标转换
            QPointF individualScreenPos = m_cameraController->toScreenCoords(QPointF(individual->position.x, individual->position.y), size());
            double distSq = QLineF(screenPos, individualScreenPos).length() * QLineF(screenPos, individualScreenPos).length();
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                foundEntity = individual;
            }
        }
    };

    // 从后往前检查，优先选中绘制在上面的生物
    for (auto it = data->race_lists.rbegin(); it != data->race_lists.rend(); ++it) {
        checkList(it->second);
    }
    for (auto it = data->thing_lists.rbegin(); it != data->thing_lists.rend(); ++it) {
        checkList(it->second);
    }

    return foundEntity;
}

void Widget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_cameraController) {
        m_cameraController->clampToBounds(event->size());
    }
    update();
}