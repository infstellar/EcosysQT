#include "Widget.h"
#include "simulation.h"
#include "race_base.h"
#include "thing_base.h"
#include "animal.h" 
#include <QPainter>
#include <QDebug>
#include <algorithm>
// --- 新增：包含鼠标事件头文件 ---
#include <QWheelEvent>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QInputDialog> // <-- 新增：包含输入对话框头文件
#include <QGridLayout>  // 确保包含了 QGridLayout

// --- 新增：前向声明一个辅助函数 ---
static QPointF screenToWorldCoords(const QPointF& screenPos, const QPointF& viewCenter, double zoomFactor, const QSize& screenSize, const QSize& worldSize);
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
    , m_grassCount(0)
    , m_cowCount(0)
    , m_tigerCount(0)
    , m_timeStep(0)
    , m_currentYear(1)   // 初始化新增变量
    , m_currentDay(1)    // 初始化新增变量
    , m_currentQuadrumName("Aprimay") // 初始化新增变量
    , m_currentHour(0)
    , m_currentMinute(0)
    , m_zoomFactor(1.0)
    , m_isDragging(false)
    , m_currentSpeedLevel(2)
{
    m_backgroundImage.load(":/images/background.png");
    if (m_backgroundImage.isNull()) {
        qDebug() << "警告: 背景图加载失败，使用纯色背景";
    }
    
    // --- 新增：加载牛、老虎、草的贴图 ---
    m_cowTexture.load(":/images/cow.png");
    if (m_cowTexture.isNull()) {
        qDebug() << "警告: 牛贴图加载失败";
    }
    // --- 新增：加载公牛贴图 ---
    m_bullTexture.load(":/images/bull.png");
    if (m_bullTexture.isNull()) {
        qDebug() << "警告: 牛(公)贴图加载失败";
    }
    m_tigerTexture.load(":/images/tiger.png");
    if (m_tigerTexture.isNull()) {
        qDebug() << "警告: 老虎贴图加载失败";
    }
    m_grassTexture.load(":/images/grass.png");
    if (m_grassTexture.isNull()) {
        qDebug() << "警告: 草贴图加载失败";
    }

    // --- 创建和布局所有控制按钮 ---
    m_restartButton = new QPushButton("重新开始", this);
    m_customSpeedButton = new QPushButton("自定义速度", this); // <-- 新增
    m_pauseButton = new QPushButton("暂停", this);
    m_slowDownButton = new QPushButton("减速 (-)", this);
    m_speedUpButton = new QPushButton("加速 (+)", this);

    // 设置按钮样式
    QString buttonStyle = "QPushButton { background-color: rgba(0, 0, 0, 180); color: white; border: 1px solid white; padding: 5px; border-radius: 3px; min-width: 80px; } QPushButton:hover { background-color: rgba(255, 255, 255, 50); } QPushButton:pressed { background-color: rgba(0, 0, 0, 220); }";
    m_restartButton->setStyleSheet(buttonStyle);
    m_customSpeedButton->setStyleSheet(buttonStyle); // <-- 新增
    m_pauseButton->setStyleSheet(buttonStyle);
    m_slowDownButton->setStyleSheet(buttonStyle);
    m_speedUpButton->setStyleSheet(buttonStyle);

    // --- 核心修改：使用网格布局 (Grid Layout) 实现新布局 ---
    QGridLayout* controlsLayout = new QGridLayout();
    controlsLayout->setSpacing(5);

    // 第一行：重启按钮和自定义速度按钮
    controlsLayout->addWidget(m_restartButton,     0, 1); // 第0行，第1列
    controlsLayout->addWidget(m_customSpeedButton, 0, 2); // 第0行，第2列 (重启按钮右边)

    // 第二行：三个控制按钮
    controlsLayout->addWidget(m_slowDownButton, 1, 0); // 第1行，第0列
    controlsLayout->addWidget(m_pauseButton,    1, 1); // 第1行，第1列
    controlsLayout->addWidget(m_speedUpButton,  1, 2); // 第1行，第2列

    // 将整个控件组布局设置在主窗口的右下角
    QHBoxLayout* hLayout = new QHBoxLayout();
    hLayout->addStretch();
    hLayout->addLayout(controlsLayout);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addStretch();
    mainLayout->addLayout(hLayout);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    setLayout(mainLayout);

    // --- 连接信号和槽 ---
    connect(m_restartButton, &QPushButton::clicked, this, &Widget::onRestartClicked);
    connect(m_customSpeedButton, &QPushButton::clicked, this, &Widget::onCustomSpeedClicked); // <-- 新增
    connect(m_pauseButton, &QPushButton::clicked, this, &Widget::onPauseResumeClicked);
    connect(m_slowDownButton, &QPushButton::clicked, this, &Widget::onSlowDownClicked);
    connect(m_speedUpButton, &QPushButton::clicked, this, &Widget::onSpeedUpClicked);

    
    connect(m_updateTimer, &QTimer::timeout, this, &Widget::updateFrame);
    m_updateTimer->start(16);
    
    if (m_controller) {
        m_controller->set_target_fps(30);
        auto initialData = m_controller->get_data();
        if (initialData) {
            m_currentData = initialData;
            updateStatistics();
            // --- 新增：初始化视图中心为世界中心 ---
            m_viewCenter = QPointF(m_currentData->world_width / 2.0, m_currentData->world_height / 2.0);
        }
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
    auto newData = m_controller->get_data();
    if (newData) {
        m_currentData = newData;
    }
    
    updateStatistics();
    update();
}

/**
 * 更新统计数据
 * 
 * 从 m_currentData 中提取各物种的数量
 * 
 * 数据来源：
 * m_currentData->race_lists / thing_lists
 *   └─ key: std::string 物种名称（小写）
 *   └─ value: std::vector<std::shared_ptr<RaceBase/ThingBase>> 个体列表
 *       └─ individual->alive (bool 是否存活)
 */
void Widget::updateStatistics()
{
    if (!m_controller) {
        return;
    }

    const auto data = m_currentData;
    if (!data) {
        return;
    }
    
    m_grassCount = 0;
    m_cowCount = 0;
    m_tigerCount = 0;
    
    m_timeStep = data->time_step;
    m_currentYear = data->current_year;
    m_currentDay = data->current_day;
    m_currentQuadrumName = data->current_quadrum_name;
    m_currentHour = data->current_hour;
    m_currentMinute = data->current_minute;
    // 汇总 races 数量
    for (const auto& [name, individuals] : data->race_lists) {
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
    for (const auto& [name, individuals] : data->thing_lists) {
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

    const auto data = m_currentData;
    if (!data) {
        return;
    }
    
    // ========== 步骤1: 绘制背景和时间遮罩 ==========
    // 1.1 首先绘制基础背景图
    if (!m_backgroundImage.isNull()) {
        painter.drawPixmap(rect(), m_backgroundImage);
    } else {
        painter.fillRect(rect(), QColor(34, 139, 34)); // 回退方案
    }

    // 1.2 根据当前小时计算并绘制一个半透明的遮罩层
    {
        int alpha = 0; // 透明度 (0=完全透明, 255=完全不透明)
        const int nightAlpha = 160; // 夜晚最暗时的透明度

        // 定义一天中的四个阶段
        const int dawnStart = 4;  // 黎明开始 (4:00)
        const int dayStart = 8;   // 白天开始 (8:00)
        const int duskStart = 18; // 黄昏开始 (18:00)
        const int nightStart = 22; // 夜晚开始 (22:00)

        if (m_currentHour >= nightStart || m_currentHour < dawnStart) {
            // --- 夜晚 (22:00 - 03:59) ---
            alpha = nightAlpha;
        } else if (m_currentHour >= duskStart) {
            // --- 黄昏 (18:00 - 21:59) ---
            // 透明度从 0 (18:00) 线性增加到 nightAlpha (22:00)
            double progress = static_cast<double>(m_currentHour - duskStart) / (nightStart - duskStart);
            alpha = static_cast<int>(progress * nightAlpha);
        } else if (m_currentHour >= dayStart) {
            // --- 白天 (08:00 - 17:59) ---
            alpha = 0; // 完全明亮，无遮罩
        } else if (m_currentHour >= dawnStart) {
            // --- 黎明 (04:00 - 07:59) ---
            // 透明度从 nightAlpha (04:00) 线性减少到 0 (08:00)
            double progress = static_cast<double>(m_currentHour - dawnStart) / (dayStart - dawnStart);
            alpha = static_cast<int>((1.0 - progress) * nightAlpha);
        }

        // 限制 alpha 在有效范围内
        alpha = std::clamp(alpha, 0, 255);

        // 绘制遮罩
        if (alpha > 0) {
            painter.fillRect(rect(), QColor(0, 0, 30, alpha)); // 使用深蓝色调的遮罩，效果更自然
        }
    }
    
    // ========== 步骤2: 收集、排序并绘制所有生物 ==========

    // 定义一个结构体来存储绘制所需的信息
    struct DrawableEntity {
        const QPixmap* texture;
        QRect targetRect;
        double worldY; // 用于排序
    };

    std::vector<DrawableEntity> entitiesToDraw;
    entitiesToDraw.reserve(m_grassCount + m_cowCount + m_tigerCount); // 预分配内存以提高效率

    // 循环 1: 收集 Races (动物)
    for (const auto& [name, individuals] : data->race_lists) {
        for (const auto& individual_base : individuals) {
            if (!individual_base || !individual_base->alive) continue;

            const QPixmap* texture = nullptr;
            
            // --- 核心修改：根据物种和性别选择贴图 ---
            if (name == "cow") {
                // 尝试将 RaceBase 指针安全地转换为 Animal 指针以访问性别
                auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
                if (animal_ptr) { // 转换成功
                    texture = (animal_ptr->sex == Sex::MALE) ? &m_bullTexture : &m_cowTexture;
                } else { // 转换失败（理论上不应发生），使用默认母牛贴图
                    texture = &m_cowTexture;
                }
            } else if (name == "tiger") {
                texture = &m_tigerTexture;
            }
            // --- 修改结束 ---

            if (!texture || texture->isNull()) continue;

            QPointF screenPos = toScreenCoords(individual_base->position);
            const double max_energy = std::max(1.0, individual_base->max_energy);
            const double energyRatio = std::clamp(individual_base->energy / max_energy, 0.0, 1.5);
            const double size = 40.0 + energyRatio * 12.0;
            QRectF targetRectF(screenPos.x() - size / 2, screenPos.y() - size / 2, size, size);
            
            entitiesToDraw.push_back({texture, targetRectF.toRect(), individual_base->position.y});
        }
    }

    // 循环 2: 收集 Things (植物)
    for (const auto& [name, individuals] : data->thing_lists) {
        if (name == "grass") {
            if (m_grassTexture.isNull()) continue;
            for (const auto& individual : individuals) {
                if (!individual || !individual->alive) continue;
                QPointF screenPos = toScreenCoords(individual->position);
                const double size = 20.0;
                QRectF targetRectF(screenPos.x() - size / 2, screenPos.y() - size / 2, size, size);
                entitiesToDraw.push_back({&m_grassTexture, targetRectF.toRect(), individual->position.y});
            }
        }
    }

    // 排序：根据世界坐标的Y值从小到大排序，解决遮挡问题
    std::sort(entitiesToDraw.begin(), entitiesToDraw.end(), [](const DrawableEntity& a, const DrawableEntity& b) {
        return a.worldY < b.worldY;
    });

    // 循环 3: 按排序后的顺序绘制所有实体
    for (const auto& entity : entitiesToDraw) {
        painter.drawPixmap(entity.targetRect, *entity.texture);
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

    // ========== 步骤4: 绘制右上角时间 ==========
    {
        // 格式化时间字符串，例如 07:09
        QString timeString = QString("%1:%2")
                                 .arg(m_currentHour, 2, 10, QChar('0'))
                                 .arg(m_currentMinute, 2, 10, QChar('0'));

        // 设置字体和颜色
        QFont timeFont("Arial", 16, QFont::Bold);
        painter.setFont(timeFont);
        painter.setPen(Qt::white);

        // 计算文本绘制位置，使其右对齐
        QFontMetrics fm(timeFont);
        int textWidth = fm.horizontalAdvance(timeString);
        int margin = 15;
        int x = width() - textWidth - margin;
        int y = 35; // 与信息面板顶部对齐

        // 绘制带阴影的文本以增加可读性
        painter.setPen(QColor(0, 0, 0, 120));
        painter.drawText(x + 2, y + 2, timeString); // 阴影
        painter.setPen(Qt::white);
        painter.drawText(x, y, timeString); // 主文本
    }
}

// --- 新增：实现 wheelEvent 函数 ---
/**
 * 鼠标滚轮事件处理函数
 * 
 * @param event 滚轮事件对象
 * 
 * 逻辑：
 * 1. 获取鼠标当前在屏幕上的位置。
 * 2. 将该屏幕位置转换为缩放前的世界坐标。
 * 3. 根据滚轮方向，计算新的缩放因子 m_zoomFactor。
 * 4. 将该屏幕位置转换为缩放后的世界坐标。
 * 5. 计算两次世界坐标的差值，并用这个差值来平移视图中心 m_viewCenter。
 * 6. 触发界面重绘。
 * 
 * 效果：实现以鼠标指针为中心的缩放。
 */
void Widget::wheelEvent(QWheelEvent *event)
{
    const auto data = m_currentData;
    if (!data) {
        event->ignore();
        return;
    }

    const QPointF mousePos = event->position();
    const QSize worldSize(data->world_width, data->world_height);

    // 1. 记录缩放前的世界坐标
    const QPointF worldPosBeforeZoom = screenToWorldCoords(mousePos, m_viewCenter, m_zoomFactor, size(), worldSize);

    // 2. 计算新的缩放因子
    const double zoomStep = 1.15;
    if (event->angleDelta().y() > 0) {
        m_zoomFactor *= zoomStep;
    } else {
        m_zoomFactor /= zoomStep;
    }
    m_zoomFactor = std::clamp(m_zoomFactor, 0.1, 20.0);

    // 3. 记录缩放后的世界坐标
    const QPointF worldPosAfterZoom = screenToWorldCoords(mousePos, m_viewCenter, m_zoomFactor, size(), worldSize);

    // 4. 移动视图中心，以保持鼠标下的点位置不变
    m_viewCenter += (worldPosBeforeZoom - worldPosAfterZoom);

    update(); // 请求重绘
}

// --- 新增：实现鼠标按下事件 ---
/**
 * 鼠标按下事件处理函数
 * 
 * @param event 鼠标事件对象
 * 
 * 逻辑：
 * 1. 检查是否是鼠标中键被按下。
 * 2. 如果是，则将 m_isDragging 设为 true，并记录当前鼠标位置。
 * 3. 设置鼠标光标为“抓手”形状，提供视觉反馈。
 */
void Widget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_isDragging = true;
        m_lastMousePos = event->localPos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        event->ignore();
    }
}

// --- 新增：实现鼠标移动事件 ---
/**
 * 鼠标移动事件处理函数
 * 
 * @param event 鼠标事件对象
 * 
 * 逻辑：
 * 1. 检查 m_isDragging 是否为 true。
 * 2. 如果是，则计算鼠标从上一次位置移动的偏移量（屏幕坐标）。
 * 3. 将这个屏幕偏移量转换为世界坐标下的偏移量。
 * 4. 从视图中心 m_viewCenter 中减去这个世界偏移量，实现视图的平移。
 * 5. 更新上一次鼠标位置。
 * 6. 触发重绘。
 */
void Widget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        const auto data = m_currentData;
        if (!data) {
            event->ignore();
            return;
        }

        QPointF delta = event->localPos() - m_lastMousePos;

        // --- 修改：实现等比缩放下的拖动计算 ---
        // 1. 获取等比缩放后的可见世界尺寸
        double visibleWorldWidth = data->world_width / m_zoomFactor;
        double screenAspect = (double)width() / (double)height();
        double visibleWorldHeight = visibleWorldWidth / screenAspect;

        // 2. 根据可见尺寸计算世界坐标的偏移量
        double worldDeltaX = (delta.x() / width()) * visibleWorldWidth;
        double worldDeltaY = (delta.y() / height()) * visibleWorldHeight;
        // --- 修改结束 ---

        // 视图中心向相反方向移动
        m_viewCenter -= QPointF(worldDeltaX, worldDeltaY);

        m_lastMousePos = event->localPos();
        update();
        event->accept();
    } else {
        event->ignore();
    }
}

// --- 新增：实现鼠标释放事件 ---
/**
 * 鼠标释放事件处理函数
 * 
 * @param event 鼠标事件对象
 * 
 * 逻辑：
 * 1. 检查是否是鼠标中键被释放。
 * 2. 如果是，则将 m_isDragging 设为 false，并恢复鼠标光标形状。
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
    const auto data = m_currentData;
    if (!data || data->world_width <= 0 || data->world_height <= 0) {
        return QPointF();
    }

    // --- 修改：实现等比缩放下的坐标转换 ---
    // 1. 计算当前缩放级别下，视图在世界坐标系中的可见宽度
    double visibleWorldWidth = data->world_width / m_zoomFactor;
    // 修复：可见高度必须根据可见宽度和屏幕宽高比计算，以保持等比缩放
    double screenAspect = (double)width() / (double)height();
    double visibleWorldHeight = visibleWorldWidth / screenAspect;
    // --- 修改结束 ---

    // 2. 计算视图在世界坐标系中的左上角坐标
    double viewLeft = m_viewCenter.x() - visibleWorldWidth / 2.0;
    double viewTop = m_viewCenter.y() - visibleWorldHeight / 2.0;

    // 3. 计算目标点相对于视图左上角的偏移
    double relativeX = pos.x - viewLeft;
    double relativeY = pos.y - viewTop;

    // 4. 将相对偏移按比例映射到屏幕坐标
    double screenX = (relativeX / visibleWorldWidth) * width();
    double screenY = (relativeY / visibleWorldHeight) * height();

    return QPointF(screenX, screenY);
}

// --- 新增：实现屏幕到世界坐标的转换辅助函数 ---
/**
 * 将屏幕坐标转换为世界坐标
 * 
 * 这是 toScreenCoords 的逆运算
 */
static QPointF screenToWorldCoords(const QPointF& screenPos, const QPointF& viewCenter, double zoomFactor, const QSize& screenSize, const QSize& worldSize)
{
    if (worldSize.width() <= 0 || worldSize.height() <= 0) {
        return QPointF();
    }

    // --- 修改：实现等比缩放下的逆向坐标转换 ---
    double visibleWorldWidth = worldSize.width() / zoomFactor;
    // 修复：逻辑必须与 toScreenCoords 保持一致
    double screenAspect = (double)screenSize.width() / (double)screenSize.height();
    double visibleWorldHeight = visibleWorldWidth / screenAspect;
    // --- 修改结束 ---

    double viewLeft = viewCenter.x() - visibleWorldWidth / 2.0;
    double viewTop = viewCenter.y() - visibleWorldHeight / 2.0;

    double relativeX = (screenPos.x() / screenSize.width()) * visibleWorldWidth;
    double relativeY = (screenPos.y() / screenSize.height()) * visibleWorldHeight;

    return QPointF(viewLeft + relativeX, viewTop + relativeY);
}

// --- 新增：实现按钮的槽函数 ---

void Widget::onRestartClicked()
{
    if (!m_controller) return;

    qDebug() << "请求重新开始模拟...";

    // 1. 创建一个新的、与 main.cpp 中相同的默认配置
    EcosystemConfig newConfig(800, 600);
    newConfig.initial_populations = {
        {"grass", 800},
        {"cow", 3},
        {"tiger", 0},
    };

    // 2. 调用后端的 reset 方法
    m_controller->reset(newConfig);

    // 3. 重置前端UI状态
    m_zoomFactor = 1.0; // 恢复默认缩放
    m_viewCenter = QPointF(newConfig.world_width / 2.0, newConfig.world_height / 2.0); // 视图回到中心
    m_currentSpeedLevel = 4; // 恢复默认速度等级
    if (m_controller->is_paused()) {
        m_pauseButton->setText("继续");
    } else {
        m_pauseButton->setText("暂停");
    }


    // 4. 立即获取一次新数据并刷新界面
    updateFrame();
}

void Widget::onPauseResumeClicked()
{
    if (!m_controller) return;

    if (m_controller->is_paused()) {
        m_controller->resume();
        m_pauseButton->setText("暂停");
    } else {
        m_controller->pause();
        m_pauseButton->setText("继续");
    }
}

void Widget::onSpeedUpClicked()
{
    if (!m_controller) return;
    m_currentSpeedLevel++;
    // --- 修改：定义新的10级速度映射表 (1-100 FPS) ---
    const std::map<int, int> speedMap = {
        {0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, 
        {5, 60}, {6, 70}, {7, 80}, {8, 90}, {9, 100}
    };
    
    // --- 修改：更新速度等级上限为 9 ---
    if (m_currentSpeedLevel > 9) m_currentSpeedLevel = 9;

    auto it = speedMap.find(m_currentSpeedLevel);
    if (it != speedMap.end()) {
        m_controller->set_target_fps(it->second);
        qDebug() << "速度等级:" << m_currentSpeedLevel << ", FPS:" << it->second;
    }
}

void Widget::onSlowDownClicked()
{
    if (!m_controller) return;
    m_currentSpeedLevel--;
    // --- 修改：定义新的10级速度映射表 (1-100 FPS) ---
    const std::map<int, int> speedMap = {
        {0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, 
        {5, 60}, {6, 70}, {7, 80}, {8, 90}, {9, 100}
    };
    if (m_currentSpeedLevel < 0) m_currentSpeedLevel = 0;

    auto it = speedMap.find(m_currentSpeedLevel);
    if (it != speedMap.end()) {
        m_controller->set_target_fps(it->second);
        qDebug() << "速度等级:" << m_currentSpeedLevel << ", FPS:" << it->second;
    }
}

// --- 新增：实现自定义速度按钮的槽函数 ---
void Widget::onCustomSpeedClicked()
{
    if (!m_controller) return;

    // 定义速度等级与FPS的映射关系，用于获取当前速度作为默认值
    const std::map<int, int> speedMap = {
        {0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, 
        {5, 60}, {6, 70}, {7, 80}, {8, 90}, {9, 100}
    };
    int currentFps = 30; // 默认值
    auto it = speedMap.find(m_currentSpeedLevel);
    if (it != speedMap.end()) {
        currentFps = it->second;
    }

    bool ok;
    int newFps = QInputDialog::getInt(this, "设置模拟速度",
                                      "请输入目标 FPS (1-2000):",
                                      currentFps, // 对话框的默认值
                                      1,          // 最小值
                                      2000,        // 最大值
                                      1,          // 步长
                                      &ok);

    if (ok) {
        // 用户点击了“确定”
        m_controller->set_target_fps(newFps);
        
        // 可选：尝试反向更新速度等级，使UI状态同步
        int closestLevel = -1;
        int minDiff = std::numeric_limits<int>::max();
        for(const auto& pair : speedMap) {
            int diff = std::abs(pair.second - newFps);
            if (diff < minDiff) {
                minDiff = diff;
                closestLevel = pair.first;
            }
        }
        if (closestLevel != -1) {
            m_currentSpeedLevel = closestLevel;
        }

        qDebug() << "自定义速度已设置为:" << newFps << "FPS";
    }
}