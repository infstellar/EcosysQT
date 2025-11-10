#ifndef SIMULATIONRENDERER_H
#define SIMULATIONRENDERER_H

#include <QPainter>
#include <memory>
#include "ecosystem.h"
#include "Widget.h" // 为了使用 SelectableEntity 和访问 Widget 成员
#include <unordered_map>
#include <random>

// 前向声明
class CameraController;

/**
 * @class SimulationRenderer
 * @brief 负责将生态系统的状态绘制到屏幕上。
 *
 * 这个类封装了所有与渲染相关的逻辑，包括加载纹理、绘制背景、
 * 绘制所有生物实体、绘制信息面板（HUD）以及处理选中和悬停效果。
 * 它从 Widget 类中分离出来，以实现关注点分离。
 */
class SimulationRenderer
{
public:
    explicit SimulationRenderer(Widget* parentWidget);

    /**
     * @brief 主渲染函数，在 Widget::paintEvent 中被调用。
     * @param painter Qt 的绘图工具。
     * @param data 当前的模拟数据快照。
     * @param camera 相机控制器，用于坐标转换。
     * @param hovered 当前悬停的实体。
     * @param selected 当前选中的实体。
     * @param isInspectMode 是否处于查看模式。
     */
    void render(QPainter& painter,
                const std::shared_ptr<EcosystemStateData>& data,
                const CameraController& camera,
                const std::optional<SelectableEntity>& hovered,
                const std::optional<SelectableEntity>& selected,
                bool isInspectMode);

private:
    // ========== 私有绘制函数 ==========
    void drawBackground(QPainter& painter);
    void drawEntities(QPainter& painter, const std::shared_ptr<EcosystemStateData>& data, const CameraController& camera);
    void drawHud(QPainter& painter);
    void drawSelection(QPainter& painter, const CameraController& camera, const std::optional<SelectableEntity>& hovered, const std::optional<SelectableEntity>& selected);
    void drawSelectionInfo(QPainter& painter, const CameraController& camera, const SelectableEntity& entity);

    // ========== 辅助函数 ==========
    QColor getColorForName(const std::string& name) const;

    // ========== 成员变量 ==========
    Widget* m_parentWidget; // 用于访问父窗口的属性，如尺寸和缓存的统计数据

    // ========== UI 资源 ==========
    QPixmap m_backgroundImage;
    QPixmap m_cowTexture;
    QPixmap m_bullTexture;
    QPixmap m_tigerTexture;
    QPixmap m_tigerManTexture;
    QPixmap m_grassTextures[3];
    // UI 层缓存：为每个 ThingBase 指针分配的草贴图变体（确保稳定但随机）
    std::unordered_map<const ThingBase*, int> m_grassVariantMap;
    // 用于在首次遇到时随机分配变体
    std::mt19937 m_rng;
};

#endif // SIMULATIONRENDERER_H