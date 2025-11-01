#include "Widget.h"
#include <QPainter>
#include <QDebug>

Widget::Widget(IBackendInterface *backend, QWidget *parent)
    : QWidget(parent)
    , m_backend(backend)
    , m_updateTimer(new QTimer(this))
    , m_grassCount(0)
    , m_herbivoreCount(0)
    , m_carnivoreCount(0)
    , m_omnivoreCount(0)
{
    // 加载背景图（使用 qrc 路径 /images/grass.png）
    m_backgroundImage.load(":/images/grass.png");
    if (m_backgroundImage.isNull()) {
        qDebug() << "警告: 背景图加载失败";
    }
    
    // 启动运行时间计时器
    m_elapsedTimer.start();
    
    // 设置更新定时器 (每秒调用一次后端)
    connect(m_updateTimer, &QTimer::timeout, this, &Widget::updateFrame);
    m_updateTimer->start(1000);  // 1000ms = 1秒
}

Widget::~Widget() {}

void Widget::updateFrame()
{
    if (m_backend) {
        // 调用后端接口获取新数据
        m_currentData = m_backend->getNextFrame();
        updateStatistics();
        update();  // 触发重绘
    }
}

void Widget::updateStatistics()
{
    m_grassCount = 0;
    m_herbivoreCount = 0;
    m_carnivoreCount = 0;
    m_omnivoreCount = 0;
    
    for (const DataItem &item : m_currentData) {
        switch (item.type) {
            case SpeciesType::Grass:
                m_grassCount++;
                break;
            case SpeciesType::Herbivore:
                m_herbivoreCount++;
                break;
            case SpeciesType::Carnivore:
                m_carnivoreCount++;
                break;
            case SpeciesType::Omnivore:
                m_omnivoreCount++;
                break;
        }
    }
}

void Widget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 1. 绘制背景图
    if (!m_backgroundImage.isNull()) {
        painter.drawPixmap(rect(), m_backgroundImage);
    } else {
        painter.fillRect(rect(), QColor(34, 139, 34));
    }
    
    // 2. 绘制动物 (草不绘制，作为背景)
    for (const DataItem &item : m_currentData) {
        if (item.type != SpeciesType::Grass) {
            QPointF screenPos = toScreenCoords(item.x, item.y);
            QColor color = getColorForType(item.type);
            
            painter.setBrush(color);
            painter.setPen(QPen(Qt::white, 2));
            painter.drawEllipse(screenPos, 8, 8);
        }
    }
    
    // 3. 绘制信息面板
    QRectF infoRect(10, 10, 250, 120);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(infoRect, 5, 5);
    
    // 4. 绘制文字信息
    painter.setPen(Qt::white);
    QFont font("Arial", 12, QFont::Bold);
    painter.setFont(font);
    
    int textY = 30;
    int lineHeight = 20;
    
    painter.drawText(20, textY, "运行时间: " + formatElapsedTime());
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("总数量: %1").arg(m_currentData.size()));
    textY += lineHeight;
    
    painter.drawText(20, textY, "草: ");
    painter.fillRect(90, textY - 12, 15, 15, getColorForType(SpeciesType::Grass));
    painter.drawText(110, textY, QString::number(m_grassCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "食草: ");
    painter.fillRect(90, textY - 12, 15, 15, getColorForType(SpeciesType::Herbivore));
    painter.drawText(110, textY, QString::number(m_herbivoreCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "食肉: ");
    painter.fillRect(90, textY - 12, 15, 15, getColorForType(SpeciesType::Carnivore));
    painter.drawText(110, textY, QString::number(m_carnivoreCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "杂食: ");
    painter.fillRect(90, textY - 12, 15, 15, getColorForType(SpeciesType::Omnivore));
    painter.drawText(110, textY, QString::number(m_omnivoreCount));
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
        case SpeciesType::Grass:
            return QColor(34, 139, 34);
        case SpeciesType::Herbivore:
            return QColor(135, 206, 250);
        case SpeciesType::Carnivore:
            return QColor(220, 20, 60);
        case SpeciesType::Omnivore:
            return QColor(255, 165, 0);
        default:
            return Qt::black;
    }
}

QPointF Widget::toScreenCoords(float x, float y) const
{
    float screenX = (x + 100) / 200.0 * width();
    float screenY = (100 - y) / 200.0 * height();
    return QPointF(screenX, screenY);
}