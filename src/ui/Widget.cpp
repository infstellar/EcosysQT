#include "Widget.h"
#include "simulation.h"
#include <QPainter>
#include <QDebug>

/**
 * 构造函数实现
 * 
 * @param controller 后端模拟控制器指针（由 main.cpp 传入）
 * 
 * 数据流程说明：
 * SimulationController (后端)
 *   └─ SimulationEngine (unique_ptr)
 *       └─ EcosystemState (unique_ptr)
 *           └─ SpeciesRegistry (所有生物数据)
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
{
    m_backgroundImage.load(":/images/grass.png");
    if (m_backgroundImage.isNull()) {
        qDebug() << "警告: 背景图加载失败，使用纯色背景";
    }
    
    m_elapsedTimer.start();
    
    connect(m_updateTimer, &QTimer::timeout, this, &Widget::updateFrame);
    m_updateTimer->start(1000);
    
    if (m_controller) {
        m_currentData = m_controller->get_data();
        updateStatistics();
    }
    
    // ========== 创建控制按钮 ==========
    /**
     * 创建三个控制按钮：暂停、继续、重启
     * 
     * 布局位置：窗口右上角
     * 按钮尺寸：80x30 像素
     * 间距：10 像素
     * 
     * 信号连接：
     * - 暂停按钮 → controller->pause()   暂停模拟更新
     * - 继续按钮 → controller->resume()  恢复模拟更新
     * - 重启按钮 → controller->reset()   重置生态系统到初始状态
     * 
     * 注意：
     * - 按钮的父对象是 this，Qt 会自动管理内存
     * - 使用 setGeometry 设置绝对位置，不使用布局管理器
     * - 按钮会自动显示在绘制内容的上层
     */
    
    // 创建暂停按钮（右上角第一个）
    m_pauseButton = new QPushButton("暂停", this);
    m_pauseButton->setGeometry(width() - 280, 10, 80, 30);
    
    // 创建继续按钮（右上角第二个）
    m_resumeButton = new QPushButton("继续", this);
    m_resumeButton->setGeometry(width() - 190, 10, 80, 30);
    
    // 创建重启按钮（右上角第三个）
    m_restartButton = new QPushButton("重启", this);
    m_restartButton->setGeometry(width() - 100, 10, 80, 30);
    
    // 连接按钮信号到控制器的方法（使用 lambda 函数）
    connect(m_pauseButton, &QPushButton::clicked, 
            [this]() {
                if (m_controller) {
                    m_controller->pause();
                    qDebug() << "模拟已暂停";
                }
            });
    
    connect(m_resumeButton, &QPushButton::clicked, 
            [this]() {
                if (m_controller) {
                    m_controller->resume();
                    qDebug() << "模拟已继续";
                }
            });
    
    connect(m_restartButton, &QPushButton::clicked, 
            [this]() {
                if (m_controller) {
                    // 重启需要传递配置参数，这里使用当前配置
                    EcosystemConfig config(
                        m_currentData.world_width,
                        m_currentData.world_height,
                        100, 10, 2  // 初始草、牛、老虎数量
                    );
                    m_controller->reset(config);
                    qDebug() << "模拟已重启";
                }
            });
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
    /**
     * get_data() 返回 EcosystemStateData
     * 
     * 后端数据结构（定义在 backend/include/utils.h）：
     * 
     * struct EcosystemStateData {
     *     int world_width;                                              // 世界宽度（800）
     *     int world_height;                                             // 世界高度（600）
     *     std::map<std::string, std::vector<std::shared_ptr<Species>>> species_lists;  // 物种 map
     *     int time_step;                                                // 当前时间步
     *     Eigen::MatrixXd grass_positions_array;                        // 草的位置矩阵（优化用）
     *     std::vector<std::shared_ptr<Species>> alive_grass_objects;    // 存活的草对象
     * };
     * 
     * species_lists 的结构（关键！）：
     * std::map<std::string, std::vector<std::shared_ptr<Species>>>
     * {
     *     "grass": [Species智能指针1, Species智能指针2, ...],  // ← 注意：小写！
     *     "cow":   [Species智能指针1, Species智能指针2, ...],
     *     "tiger": [Species智能指针1, Species智能指针2, ...]
     * }
     * 
     * Species 基类的成员（定义在 species.h）：
     * - Position position        {double x, double y}
     * - double energy            当前能量值
     * - double max_energy        最大能量值
     * - int age                  年龄（时间步数）
     * - bool alive               是否存活
     * - std::string species_name 物种名称
     * 
     * 注意：
     * - 这是数据快照，但包含智能指针（共享所有权）
     * - 前端可以安全读取 Species 对象的属性
     * - 后端在独立线程中修改原始数据
     */
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
 * m_currentData.species_lists (std::map<std::string, std::vector<std::shared_ptr<Species>>>)
 *   └─ map 的 key   (std::string 物种名称 "grass"/"cow"/"tiger") ← 注意：后端使用小写！
 *   └─ map 的 value (std::vector<std::shared_ptr<Species>> 个体列表)
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
    
    // ========== 遍历 map：物种名称 -> 个体列表 ==========
    /**
     * C++17 结构化绑定语法：
     * for (const auto& [key, value] : map) {...}
     * 
     * 等价于：
     * for (const auto& pair : map) {
     *     const std::string& species_name = pair.first;
     *     const std::vector<std::shared_ptr<Species>>& individuals = pair.second;
     * }
     * 
     * species_name 的可能值（后端实际使用的键名）：
     * - "grass"  草（注意：小写！）
     * - "cow"    牛（注意：小写！）
     * - "tiger"  老虎（注意：小写！）
     */
    for (const auto& [species_name, individuals] : m_currentData.species_lists) {
        // species_name: const std::string& ("grass", "cow", "tiger")
        // individuals:  const std::vector<std::shared_ptr<Species>>&
        
        int alive_count = 0;
        for (const auto& individual : individuals) {
            // individual: const std::shared_ptr<Species>&
            
            // 检查智能指针是否有效，并且生物是否存活
            if (individual && individual->alive) {
                alive_count++;
            }
        }
        
        // ✅ 修复：根据小写的物种名称更新对应的计数器
        if (species_name == "grass") {
            m_grassCount = alive_count;
        } else if (species_name == "cow") {
            m_cowCount = alive_count;
        } else if (species_name == "tiger") {
            m_tigerCount = alive_count;
        }
    }
}

/**
 * 绘图事件处理函数
 * 
 * 绘制流程：
 * 1. 绘制背景
 * 2. 遍历 species_lists map，绘制所有生物
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
    /**
     * 数据来源：m_currentData.species_lists
     * 数据结构：std::map<std::string, std::vector<std::shared_ptr<Species>>>
     * 
     * map 的结构：
     * {
     *     "grass": [Species对象指针1, Species对象指针2, ...],
     *     "cow":   [Species对象指针1, Species对象指针2, ...],
     *     "tiger": [Species对象指针1, Species对象指针2, ...]
     * }
     * 
     * Species 基类的成员：
     * - Position position        {double x, double y}
     * - double energy            当前能量值
     * - double max_energy        最大能量值
     * - int age                  年龄
     * - bool alive               是否存活
     * - std::string species_name 物种名称
     * 
     * 渲染策略：
     * - 只渲染 alive == true 的个体
     * - 将世界坐标转换为屏幕坐标
     * - 根据物种类型选择颜色和绘制方式
     */
    for (const auto& [species_name, individuals] : m_currentData.species_lists) {
        // species_name: const std::string&
        // individuals:  const std::vector<std::shared_ptr<Species>>&
        
        SpeciesType type = getSpeciesTypeFromName(species_name);
        QColor color = getColorForType(type);
        
        for (const auto& individual : individuals) {
            // individual: const std::shared_ptr<Species>&
            
            if (!individual || !individual->alive) {
                continue;
            }
            
            // individual->position 是 Position 类型 {double x, double y}
            QPointF screenPos = toScreenCoords(individual->position);
            
            if (type == SpeciesType::GRASS) {
                // ========== 草：绘制小点 ==========
                painter.setBrush(color);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(screenPos, 3, 3);
            } else {
                // ========== 动物（牛/老虎）：绘制圆圈 ==========
                /**
                 * 大小根据能量比例调整：
                 * - energyRatio = energy / max_energy
                 * - radius = 6 + energyRatio * 4 (范围 [6, 10])
                 */
                double energyRatio = individual->energy / individual->max_energy;
                double radius = 6 + energyRatio * 4;
                
                painter.setBrush(color);
                painter.setPen(QPen(Qt::white, 2));
                painter.drawEllipse(screenPos, radius, radius);
            }
        }
    }
    
    // ========== 步骤3: 绘制信息面板 ==========
    /**
     * 信息面板布局：
     * 
     * ┌────────────────────────────┐
     * │ 运行时间: 00:05:23         │ ← 30px
     * │ 时间步: 1500               │ ← 54px
     * │ 总数量: 85                 │ ← 78px
     * │ 草:  █ 60                  │ ← 102px
     * │ 牛:  █ 20                  │ ← 126px
     * │ 老虎: █ 5                  │ ← 150px
     * └────────────────────────────┘
     */
    QRectF infoRect(10, 10, 280, 160);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(infoRect, 5, 5);
    
    painter.setPen(Qt::white);
    QFont font("Arial", 12, QFont::Bold);
    painter.setFont(font);
    
    int textY = 30;
    int lineHeight = 24;
    
    painter.drawText(20, textY, "运行时间: " + formatElapsedTime());
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("时间步: %1").arg(m_timeStep));
    textY += lineHeight;
    
    int totalCount = m_grassCount + m_cowCount + m_tigerCount;
    painter.drawText(20, textY, QString("总数量: %1").arg(totalCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "草: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForType(SpeciesType::GRASS));
    painter.drawText(95, textY, QString::number(m_grassCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "牛: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForType(SpeciesType::COW));
    painter.drawText(95, textY, QString::number(m_cowCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "老虎: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForType(SpeciesType::TIGER));
    painter.drawText(95, textY, QString::number(m_tigerCount));
}

QString Widget::formatElapsedTime() const
{
    qint64 elapsed = m_elapsedTimer.elapsed();
    int seconds = (elapsed / 1000) % 60;
    int minutes = (elapsed / 60000) % 60;
    int hours = (elapsed / 3600000);
    
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QColor Widget::getColorForType(SpeciesType type) const
{
    switch (type) {
        case SpeciesType::GRASS:
            return QColor(144, 238, 144);
        case SpeciesType::COW:
            return QColor(135, 206, 250);
        case SpeciesType::TIGER:
            return QColor(220, 20, 60);
        default:
            return Qt::gray;
    }
}

QString Widget::getNameForType(SpeciesType type) const
{
    switch (type) {
        case SpeciesType::GRASS:
            return "草";
        case SpeciesType::COW:
            return "牛";
        case SpeciesType::TIGER:
            return "老虎";
        default:
            return "未知";
    }
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

/**
 * 从物种名称转换为 SpeciesType 枚举
 * 
 * @param species_name 物种名称字符串（后端使用小写："grass", "cow", "tiger"）
 * @return SpeciesType 枚举值
 * 
 * 映射关系（注意：后端使用小写键名）：
 * - "grass" → SpeciesType::GRASS
 * - "cow"   → SpeciesType::COW
 * - "tiger" → SpeciesType::TIGER
 * - 其他    → SpeciesType::GRASS (默认)
 */
SpeciesType Widget::getSpeciesTypeFromName(const std::string& species_name) const
{
    // ✅ 修复：匹配后端使用的小写键名
    if (species_name == "grass") {
        return SpeciesType::GRASS;
    } else if (species_name == "cow") {
        return SpeciesType::COW;
    } else if (species_name == "tiger") {
        return SpeciesType::TIGER;
    }
    
    return SpeciesType::GRASS;  // 默认值
}