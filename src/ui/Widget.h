#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QPaintEvent>
#include <QTimer>
#include <QElapsedTimer>
#include "DataStructures.h"
#include "IBackendInterface.h"

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(IBackendInterface *backend, QWidget *parent = nullptr);
    ~Widget();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateFrame();  // 每秒更新一次

private:
    IBackendInterface *m_backend;  // 后端接口指针
    DataPacket m_currentData;
    QPixmap m_backgroundImage;
    QElapsedTimer m_elapsedTimer;
    QTimer *m_updateTimer;
    
    // 统计信息
    int m_grassCount;
    int m_herbivoreCount;
    int m_carnivoreCount;
    int m_omnivoreCount;
    
    void updateStatistics();
    QColor getColorForType(SpeciesType type) const;
    QPointF toScreenCoords(float x, float y) const;
    QString formatElapsedTime() const;
};

#endif // WIDGET_H