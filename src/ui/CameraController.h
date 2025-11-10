#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include <QPointF>
#include <QSize>

// 前向声明 Qt 事件类，避免在头文件中包含大型 Qt 头文件
class QWheelEvent;
class QMouseEvent;

/**
 * @class CameraController
 * @brief 管理视图状态（缩放、平移）和处理相关的鼠标事件。
 *
 * 这个类封装了所有与“相机”或“视图”相关的逻辑，
 * 包括缩放级别、视图中心点，以及处理鼠标输入以改变这些状态。
 * 它还提供了世界坐标和屏幕坐标之间的转换功能。
 */
class CameraController
{
public:
    CameraController(double worldWidth, double worldHeight);

    // --- 事件处理器 ---
    void handleWheelEvent(QWheelEvent *event, const QSize& screenSize);
    void handleMouseMoveEventForPan(QMouseEvent *event, const QSize& screenSize);
    void setLastMousePos(const QPointF& pos); // 用于开始拖动时设置初始位置

    // --- 访问器 ---
    double getZoomFactor() const;
    const QPointF& getViewCenter() const;
    void setViewCenter(const QPointF& center);
    void setZoomFactor(double zoom);
    // 将当前缩放和平移限制在地图边界内（需要屏幕尺寸来计算可见世界尺寸）
    void clampToBounds(const QSize& screenSize);
    // 计算在给定屏幕尺寸下允许的最小缩放因子（即最远视角，能看到整张地图）
    double computeMinZoom(const QSize& screenSize) const;

    // --- 坐标转换 ---
    QPointF toScreenCoords(const QPointF& worldPos, const QSize& screenSize) const;
    QPointF toWorldCoords(const QPointF& screenPos, const QSize& screenSize) const;

private:
    double m_zoomFactor;   // 缩放因子
    QPointF m_viewCenter;  // 视图中心点（世界坐标）
    QPointF m_lastMousePos; // 用于拖动计算
    const QSize m_worldSize; // 世界的固定尺寸
};

#endif // CAMERACONTROLLER_H