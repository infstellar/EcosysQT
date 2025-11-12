#ifndef SIMULATIONRENDERER_H
#define SIMULATIONRENDERER_H

#include <QPainter>
#include <memory>
#include <optional>
#include <QPoint>
#include "ecosystem.h"
#include "Widget.h" // 为了使用 SelectableEntity 和访问 Widget 成员
#include <unordered_map>
#include <random>
#include <vector>

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
                bool isInspectMode,
                bool isGridInspectMode,
                const std::optional<QPoint>& hoveredGridCoords);

private:
    // ========== 私有绘制函数 ==========
    void drawBackground(QPainter& painter);
    void drawTerrainTiles(QPainter& painter,
                          const std::shared_ptr<EcosystemStateData>& data,
                          const CameraController& camera);
    void drawGrid(QPainter& painter, const CameraController& camera, double worldWidth, double worldHeight);
    void drawEntities(QPainter& painter, const std::shared_ptr<EcosystemStateData>& data, const CameraController& camera);
    void drawHud(QPainter& painter);
    void drawSelection(QPainter& painter, const CameraController& camera, const std::optional<SelectableEntity>& hovered, const std::optional<SelectableEntity>& selected);
    void drawSelectionInfo(QPainter& painter, const CameraController& camera, const SelectableEntity& entity);

    void drawGridInspect(QPainter& painter,
                         const std::shared_ptr<EcosystemStateData>& data,
                         const CameraController& camera,
                         const QPoint& gridCoords);

    // ========== 辅助函数 ==========
    QColor getColorForName(const std::string& name) const;

    // ========== 成员变量 ==========
    Widget* m_parentWidget; // 用于访问父窗口的属性，如尺寸和缓存的统计数据

    // ========== UI 资源 ==========
    QPixmap m_backgroundImage;
    QPixmap m_cowTexture;
    QPixmap m_bullTexture;
    // 静态回退贴图（若未切片则使用）
    QPixmap m_tigerTexture;
    QPixmap m_tigerManTexture;

    // 老虎动画帧：按 [direction][frame]
    std::vector<std::vector<QPixmap>> m_tigerFrames;
    // 每个老虎实例的动画状态（缓存上一次位置以判断朝向并推进帧）
    struct TigerAnimState {
        Position last_pos{0,0};
        double anim_timer_ms = 0.0;
        int current_frame = 0;
        int direction = 0;
        qint64 last_seen_ms = 0; // 用于清理失效条目
        bool initialized = false;
    };
    std::unordered_map<const RaceBase*, TigerAnimState> m_tigerAnimStates;
    // 配置项
    int m_tigerDirections = 4;
    int m_tigerFramesPerDir = 7;
    double m_tigerFrameIntervalMs = 120.0; // 每帧时长，毫秒
    qint64 m_lastUpdateMs = 0; // 用于计算渲染间隔
    QPixmap m_grassTextures[3];
    QPixmap m_treeTextures[3];
    // 地形贴图 atlas（用于河流等），如果使用单张 atlas 切片，可按坐标取样
    QPixmap m_riverAtlas;
    int m_riverAtlasCols = 5;
    int m_riverAtlasRows = 5;
    // 缓存按目标像素大小缩放过的树贴图，按当前渲染像素大小重建
    std::vector<QPixmap> m_treeScaled;
    int m_treeScaledSize = 0; // 当前 cached 尺寸（像素），0 表示未缓存
    // UI 层缓存：为每个 ThingBase 指针分配的草贴图变体（确保稳定但随机）
    std::unordered_map<const ThingBase*, int> m_grassVariantMap;
    // 用于在首次遇到时随机分配变体
    std::mt19937 m_rng;
    // 每种生物群系对应的贴图（按 BiomeType 的整型值索引）
    std::unordered_map<int, QPixmap> m_biomePixmaps;
};

#endif // SIMULATIONRENDERER_H