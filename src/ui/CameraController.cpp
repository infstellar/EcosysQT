#include "CameraController.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <algorithm>

CameraController::CameraController(double worldWidth, double worldHeight)
    : m_zoomFactor(1.0),
      m_viewCenter(worldWidth / 2.0, worldHeight / 2.0),
      m_worldSize(worldWidth, worldHeight)
{}

double CameraController::getZoomFactor() const { return m_zoomFactor; }
const QPointF& CameraController::getViewCenter() const { return m_viewCenter; }
void CameraController::setViewCenter(const QPointF& center) { m_viewCenter = center; }
void CameraController::setZoomFactor(double zoom) { m_zoomFactor = zoom; }
void CameraController::setLastMousePos(const QPointF& pos) { m_lastMousePos = pos; }

/**
 * 鼠标滚轮事件处理函数
 * 
 * 逻辑：
 * 1. 获取鼠标当前在屏幕上的位置。
 * 2. 将该屏幕位置转换为缩放前的世界坐标。
 * 3. 根据滚轮方向，计算新的缩放因子 m_zoomFactor。
 * 4. 将该屏幕位置转换为缩放后的世界坐标。
 * 5. 计算两次世界坐标的差值，并用这个差值来平移视图中心 m_viewCenter。
 * 
 * 效果：实现以鼠标指针为中心的缩放。
 */
void CameraController::handleWheelEvent(QWheelEvent *event, const QSize& screenSize)
{
    const QPointF mousePos = event->position();

    // 1. 记录缩放前的世界坐标
    const QPointF worldPosBeforeZoom = toWorldCoords(mousePos, screenSize);

    // 2. 计算新的缩放因子
    const double zoomStep = 1.15;
    if (event->angleDelta().y() > 0) {
        m_zoomFactor *= zoomStep;
    } else {
        m_zoomFactor /= zoomStep;
    }
    m_zoomFactor = std::clamp(m_zoomFactor, 0.1, 20.0);

    // 3. 记录缩放后的世界坐标
    const QPointF worldPosAfterZoom = toWorldCoords(mousePos, screenSize);

    // 4. 移动视图中心，以保持鼠标下的点位置不变
    m_viewCenter += (worldPosBeforeZoom - worldPosAfterZoom);
}

/**
 * 鼠标拖动平移事件处理函数
 * 
 * 逻辑：
 * 1. 计算鼠标从上一次位置移动的偏移量（屏幕坐标）。
 * 2. 将这个屏幕偏移量转换为世界坐标下的偏移量。
 * 3. 从视图中心 m_viewCenter 中减去这个世界偏移量，实现视图的平移。
 * 4. 更新上一次鼠标位置。
 */
void CameraController::handleMouseMoveEventForPan(QMouseEvent *event, const QSize& screenSize)
{
    QPointF delta = event->localPos() - m_lastMousePos;

    // --- 修改：实现等比缩放下的拖动计算 ---
    // 1. 获取等比缩放后的可见世界尺寸
    double visibleWorldWidth = m_worldSize.width() / m_zoomFactor;
    double screenAspect = (double)screenSize.width() / (double)screenSize.height();
    double visibleWorldHeight = visibleWorldWidth / screenAspect;

    // 2. 根据可见尺寸计算世界坐标的偏移量
    double worldDeltaX = (delta.x() / screenSize.width()) * visibleWorldWidth;
    double worldDeltaY = (delta.y() / screenSize.height()) * visibleWorldHeight;
    // --- 修改结束 ---

    // 视图中心向相反方向移动
    m_viewCenter -= QPointF(worldDeltaX, worldDeltaY);

    m_lastMousePos = event->localPos();
}

/**
 * 将世界坐标转换为屏幕坐标
 * 
 * 坐标系转换：
 * - 世界坐标：(0, 0) ~ (world_width, world_height)
 * - 屏幕坐标：(0, 0) ~ (窗口宽度, 窗口高度)
 */
QPointF CameraController::toScreenCoords(const QPointF& worldPos, const QSize& screenSize) const
{
    if (m_worldSize.width() <= 0 || m_worldSize.height() <= 0) {
        return QPointF();
    }

    // --- 修改：实现等比缩放下的坐标转换 ---
    // 1. 计算当前缩放级别下，视图在世界坐标系中的可见宽度
    double visibleWorldWidth = m_worldSize.width() / m_zoomFactor;
    // 修复：可见高度必须根据可见宽度和屏幕宽高比计算，以保持等比缩放
    double screenAspect = (double)screenSize.width() / (double)screenSize.height();
    double visibleWorldHeight = visibleWorldWidth / screenAspect;
    // --- 修改结束 ---

    // 2. 计算视图在世界坐标系中的左上角坐标
    double viewLeft = m_viewCenter.x() - visibleWorldWidth / 2.0;
    double viewTop = m_viewCenter.y() - visibleWorldHeight / 2.0;

    // 3. 计算目标点相对于视图左上角的偏移
    double relativeX = worldPos.x() - viewLeft;
    double relativeY = worldPos.y() - viewTop;

    // 4. 将相对偏移按比例映射到屏幕坐标
    double screenX = (relativeX / visibleWorldWidth) * screenSize.width();
    double screenY = (relativeY / visibleWorldHeight) * screenSize.height();

    return QPointF(screenX, screenY);
}

/**
 * 将屏幕坐标转换为世界坐标
 * 
 * 这是 toScreenCoords 的逆运算
 */
QPointF CameraController::toWorldCoords(const QPointF& screenPos, const QSize& screenSize) const
{
    if (m_worldSize.width() <= 0 || m_worldSize.height() <= 0) {
        return QPointF();
    }

    // --- 修改：实现等比缩放下的逆向坐标转换 ---
    double visibleWorldWidth = m_worldSize.width() / m_zoomFactor;
    // 修复：逻辑必须与 toScreenCoords 保持一致
    double screenAspect = (double)screenSize.width() / (double)screenSize.height();
    double visibleWorldHeight = visibleWorldWidth / screenAspect;
    // --- 修改结束 ---

    double viewLeft = m_viewCenter.x() - visibleWorldWidth / 2.0;
    double viewTop = m_viewCenter.y() - visibleWorldHeight / 2.0;

    double relativeX = (screenPos.x() / screenSize.width()) * visibleWorldWidth;
    double relativeY = (screenPos.y() / screenSize.height()) * visibleWorldHeight;

    return QPointF(viewLeft + relativeX, viewTop + relativeY);
}