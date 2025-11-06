#include "Widget.h"
#include "simulation.h"
#include "race_base.h"
#include "thing_base.h"
#include <QPainter>
#include <QDebug>
#include <algorithm>

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
    , m_updateTimer(new QTimer(this))
    , m_grassCount(0)
    , m_cowCount(0)
    , m_tigerCount(0)
    , m_timeStep(0)
    , m_currentYear(1)   // 初始化新增变量
    , m_currentDay(1)    // 初始化新增变量
    , m_currentQuadrumName("Aprimay") // 初始化新增变量
{
    m_backgroundImage.load(":/images/grass.png");
    if (m_backgroundImage.isNull()) {
        qDebug() << "警告: 背景图加载失败，使用纯色背景";
    }
    
    
    connect(m_updateTimer, &QTimer::timeout, this, &Widget::updateFrame);
    m_updateTimer->start(16);
    
    if (m_controller) {
        m_currentData = m_controller->get_data();
        updateStatistics();
    }
}

Widget::~Widget()
{
}

/**
 * 定时更新函数
 * 
 * 执行流程：
 * 1. 调用 controller->get_data() 获取最新的数据快照
 * 2. 更新统计数据缓存
 * 3. 触发重绘
 * 
 * 调用频率：每 1000ms (1秒) 执行一次
 */
void Widget::updateFrame()
{
    if (!m_controller) {
        qDebug() << "错误: 控制器指针为空";
        return;
    }
    
    // ========== 从后端获取数据快照 ==========
    // get_data() 返回 EcosystemStateData，提供独立的 race_lists 和 thing_lists
    // key: 物种名称（小写），value: 存活实体的共享指针列表
    m_currentData = m_controller->get_data();
    
    updateStatistics();
    update();
}

/**
 * 更新统计数据
 * 
 * 从 m_currentData 中提取各物种的数量
 * 
 * 数据来源：
 * m_currentData.race_lists / thing_lists
 *   └─ key: std::string 物种名称（小写）
 *   └─ value: std::vector<std::shared_ptr<RaceBase/ThingBase>> 个体列表
 *       └─ individual->alive (bool 是否存活)
 */
void Widget::updateStatistics()
{
    if (!m_controller) {
        return;
    }
    
    m_grassCount = 0;
    m_cowCount = 0;
    m_tigerCount = 0;
    
    m_timeStep = m_currentData.time_step;
    m_currentYear = m_currentData.current_year;
    m_currentDay = m_currentData.current_day;
    m_currentQuadrumName = m_currentData.current_quadrum_name;
    // 汇总 races 数量
    for (const auto& [name, individuals] : m_currentData.race_lists) {
        int alive_count = 0;
        for (const auto& individual : individuals) {
            if (individual && individual->alive) {
                ++alive_count;
            }
        }

        if (name == "cow") {
            m_cowCount = alive_count;
        } else if (name == "tiger") {
            m_tigerCount = alive_count;
        }
    }

    // 汇总 things 数量
    for (const auto& [name, individuals] : m_currentData.thing_lists) {
        int alive_count = 0;
        for (const auto& individual : individuals) {
            if (individual && individual->alive) {
                ++alive_count;
            }
        }

        if (name == "grass") {
            m_grassCount = alive_count;
        }
    }
}

/**
 * 绘图事件处理函数
 * 
 * 绘制流程：
 * 1. 绘制背景
 * 2. 分别遍历 race_lists 与 thing_lists 绘制所有生物
 * 3. 绘制信息面板
 */
void Widget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // ========== 步骤1: 绘制背景 ==========
    if (!m_backgroundImage.isNull()) {
        painter.drawPixmap(rect(), m_backgroundImage);
    } else {
        painter.fillRect(rect(), QColor(34, 139, 34));
    }
    
    // ========== 步骤2: 绘制所有生物 ==========
    // 循环 1: 绘制 Races (动物)
    for (const auto& [name, individuals] : m_currentData.race_lists) {
        QColor color = getColorForName(name);
        for (const auto& individual : individuals) {
            if (!individual || !individual->alive) {
                continue;
            }

            QPointF screenPos = toScreenCoords(individual->position);
            const double max_energy = std::max(1.0, individual->max_energy);
            const double energyRatio = std::clamp(individual->energy / max_energy, 0.0, 1.5);
            const double radius = 6.0 + energyRatio * 4.0;

            painter.setBrush(color);
            painter.setPen(QPen(Qt::white, 2));
            painter.drawEllipse(screenPos, radius, radius);
        }
    }

    // 循环 2: 绘制 Things (植物)
    for (const auto& [name, individuals] : m_currentData.thing_lists) {
        QColor color = getColorForName(name);
        for (const auto& individual : individuals) {
            if (!individual || !individual->alive) {
                continue;
            }

            QPointF screenPos = toScreenCoords(individual->position);
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(screenPos, 3, 3);
        }
    }
    
    // ========== 步骤3: 绘制信息面板 ==========
    /**
     * 信息面板布局：
     * 
     * ┌────────────────────────────┐
     * │ 年: 1   天: 1              │
     * │ 季: Aprimay                │
     * │ 时间步: 0                  │
     * │ 总数量: 85                 │
     * │ 草:  █ 60                  │
     * │ 牛:  █ 20                  │
     * │ 老虎: █ 5                  │
     * └────────────────────────────┘
     */
    QRectF infoRect(10, 10, 280, 184);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(infoRect, 5, 5);
    
    painter.setPen(Qt::white);
    QFont font("Arial", 12, QFont::Bold);
    painter.setFont(font);
    
    int textY = 30;
    int lineHeight = 24;
    
    painter.drawText(20, textY, QString("年: %1   天: %2").arg(m_currentYear).arg(m_currentDay));
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("季: %1").arg(QString::fromStdString(m_currentQuadrumName)));
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("时间步: %1").arg(m_timeStep));
    textY += lineHeight;
    
    int totalCount = m_grassCount + m_cowCount + m_tigerCount;
    painter.drawText(20, textY, QString("总数量: %1").arg(totalCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "草: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("grass"));
    painter.drawText(95, textY, QString::number(m_grassCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "牛: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("cow"));
    painter.drawText(95, textY, QString::number(m_cowCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "老虎: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("tiger"));
    painter.drawText(95, textY, QString::number(m_tigerCount));
}

QColor Widget::getColorForName(const std::string& name) const
{
    if (name == "grass") {
        return QColor(144, 238, 144);
    }
    if (name == "cow") {
        return QColor(135, 206, 250);
    }
    if (name == "tiger") {
        return QColor(220, 20, 60);
    }
    return Qt::gray;
}

/**
 * 将世界坐标转换为屏幕坐标
 * 
 * @param pos Position 结构（定义在 utils.h，包含 double x, double y）
 * @return QPointF 屏幕坐标
 * 
 * 坐标系转换：
 * - 世界坐标：(0, 0) ~ (world_width, world_height)
 * - 屏幕坐标：(0, 0) ~ (窗口宽度, 窗口高度)
 * 
 * 转换公式：
 * screenX = (worldX / worldWidth) * windowWidth
 * screenY = (worldY / worldHeight) * windowHeight
 * 
 * 注意：
 * - world_width 和 world_height 从 m_currentData 动态读取
 * - 支持窗口大小调整（自动缩放）
 */
QPointF Widget::toScreenCoords(const Position& pos) const
{
    double worldWidth = m_currentData.world_width;
    double worldHeight = m_currentData.world_height;
    
    double screenX = (pos.x / worldWidth) * width();
    double screenY = (pos.y / worldHeight) * height();
    
    return QPointF(screenX, screenY);
}
