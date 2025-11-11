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
    // 计算允许的最小缩放（以便整张地图可见）并 clamp
    double minZoom = computeMinZoom(screenSize);
    const double maxZoom = 60.0;
    if (minZoom <= 0.0) minZoom = 0.1; // 安全保护
    if (minZoom > maxZoom) minZoom = maxZoom;
    m_zoomFactor = std::clamp(m_zoomFactor, minZoom, maxZoom);

    // 3. 记录缩放后的世界坐标
    const QPointF worldPosAfterZoom = toWorldCoords(mousePos, screenSize);

    // 4. 移动视图中心，以保持鼠标下的点位置不变
    m_viewCenter += (worldPosBeforeZoom - worldPosAfterZoom);
    // 确保视图中心不超出地图边界
    clampToBounds(screenSize);
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
    // 限制视图中心，防止看到地图外的空白
    clampToBounds(screenSize);
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

double CameraController::computeMinZoom(const QSize& screenSize) const {
    if (m_worldSize.width() <= 0 || m_worldSize.height() <= 0) return 0.1;
    if (screenSize.width() <= 0 || screenSize.height() <= 0) return 0.1;

    // 要看到完整地图，需要同时满足可见宽度 >= worldWidth 和可见高度 >= worldHeight
    // visibleWorldWidth = m_worldSize.width() / zoom
    // visibleWorldHeight = visibleWorldWidth / screenAspect
    // 约束可得：zoom <= 1.0 （由宽度） 和 zoom <= worldWidth / (worldHeight * screenAspect) （由高度）
    double screenAspect = static_cast<double>(screenSize.width()) / static_cast<double>(screenSize.height());
    double byWidth = 1.0; // 当 zoom == 1，可见宽度恰为 worldWidth
    double byHeight = (m_worldSize.width() / (m_worldSize.height() * screenAspect));
    double minZoom = std::min(byWidth, byHeight);
    if (minZoom <= 0.0) minZoom = 0.1;
    return minZoom;
}

void CameraController::clampToBounds(const QSize& screenSize) {
    if (m_worldSize.width() <= 0 || m_worldSize.height() <= 0) return;
    if (screenSize.width() <= 0 || screenSize.height() <= 0) return;

    // 计算当前可见世界尺寸
    double visibleWorldWidth = m_worldSize.width() / m_zoomFactor;
    double screenAspect = static_cast<double>(screenSize.width()) / static_cast<double>(screenSize.height());
    double visibleWorldHeight = visibleWorldWidth / screenAspect;

    // 限制视图中心范围
    double halfW = visibleWorldWidth / 2.0;
    double halfH = visibleWorldHeight / 2.0;

    double minCenterX = halfW;
    double maxCenterX = m_worldSize.width() - halfW;
    double minCenterY = halfH;
    double maxCenterY = m_worldSize.height() - halfH;

    // 如果可见尺寸大于世界尺寸，则中心必须固定在世界中心
    if (minCenterX > maxCenterX) {
        minCenterX = maxCenterX = m_worldSize.width() / 2.0;
    }
    if (minCenterY > maxCenterY) {
        minCenterY = maxCenterY = m_worldSize.height() / 2.0;
    }

    double cx = std::clamp(m_viewCenter.x(), minCenterX, maxCenterX);
    double cy = std::clamp(m_viewCenter.y(), minCenterY, maxCenterY);
    m_viewCenter = QPointF(cx, cy);
}