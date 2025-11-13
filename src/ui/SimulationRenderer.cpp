#include "SimulationRenderer.h"
#include "CameraController.h"
#include "animal.h"
#include "thing_base.h"
#include "race_base.h"
#include "producer.h"
#include "world_grid.h"
#include "tile.h"
#ifdef ECOSIM_ENABLE_UI_DEBUG
#include "animal_ui_snapshot.h"
#endif
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QDateTime>
#include <unordered_set>
#include <cmath>
#include <QCursor>

// DrawableEntity 结构体只在渲染时使用，所以定义在这里
struct DrawableEntity {
    const QPixmap* texture;
    QRect targetRect;
    double worldY; // 用于排序
};

SimulationRenderer::SimulationRenderer(Widget* parentWidget) : m_parentWidget(parentWidget)
{
    // --- 背景：从 resources/images/backgrounds 随机选择一张（优先 Qt 资源路径，再回退到文件系统） ---
    auto loadBackgroundsFromDir = [&](const QString& dirPath) -> std::vector<QPixmap> {
        std::vector<QPixmap> out;
        QDir dir(dirPath);
        if (!dir.exists()) return out;

        QStringList nameFilters;
        nameFilters << "*.png" << "*.jpg" << "*.jpeg";
        QFileInfoList infos = dir.entryInfoList(nameFilters, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& fi : infos) {
            QPixmap p;
            // 尝试资源路径或文件系统路径按实际提供
            p.load(fi.absoluteFilePath());
            if (!p.isNull()) out.push_back(p);
        }
        return out;
    };

    // 1) 先尝试读取 Qt 资源前缀下的背景（如果背景已被打包进资源）
    //    使用 QDir(":/images/backgrounds") 可以访问资源前缀
    m_backgroundImages = loadBackgroundsFromDir(":/images/backgrounds");
    // 2) 如果资源中没有，回退到文件系统路径
    if (m_backgroundImages.empty()) {
        const QString fsDir = QString("resources/images/backgrounds");
        m_backgroundImages = loadBackgroundsFromDir(fsDir);
    }

    // 3) 如果找到至少一张，就随机选择一张作为当前背景
    if (!m_backgroundImages.empty()) {
        try {
            std::uniform_int_distribution<std::size_t> dist(0, m_backgroundImages.size() - 1);
            std::size_t idx = dist(m_rng);
            m_backgroundImage = m_backgroundImages[idx];
            qDebug() << "信息: 选择背景图片 index=" << static_cast<int>(idx) << " (total=" << static_cast<int>(m_backgroundImages.size()) << ")";
        } catch (...) {
            // 保险回退：选择第一张
            m_backgroundImage = m_backgroundImages.front();
        }
    } else {
        // 老逻辑回退：尝试旧的单张资源路径
        m_backgroundImage.load(":/images/background.png");
        if (m_backgroundImage.isNull()) {
            qDebug() << "警告: 背景图加载失败，使用纯色背景";
        }
    }
    
    m_cowTexture.load(":/images/cow.png");
    if (m_cowTexture.isNull()) {
        qDebug() << "警告: 牛贴图加载失败";
    }
    m_bullTexture.load(":/images/bull.png");
    if (m_bullTexture.isNull()) {
        qDebug() << "警告: 牛(公)贴图加载失败";
    }
    m_tigerTexture.load(":/images/tiger.png");
    if (m_tigerTexture.isNull()) {
        qDebug() << "警告: 雌性老虎贴图加载失败";
    }
    m_tigerManTexture.load(":/images/tiger_man.png");
    if (m_tigerManTexture.isNull()) {
        qDebug() << "警告: 雄性老虎贴图加载失败";
    }
    // 尝试加载新提供的老虎精灵表（4行 × 7帧）并切片
    QPixmap tigerSheet;
    tigerSheet.load(":/images/grass_variants_backup/tiger_new.png");
    if (!tigerSheet.isNull()) {
        // 如果成功加载，切片为 m_tigerFrames[direction][frame]
        QImage img = tigerSheet.toImage();
        int rows = m_tigerDirections;
        int cols = m_tigerFramesPerDir;
        if (rows > 0 && cols > 0 && img.width() >= cols && img.height() >= rows) {
            int frameW = img.width() / cols;
            int frameH = img.height() / rows;
            m_tigerFrames.assign(rows, std::vector<QPixmap>(cols));
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    QImage sub = img.copy(c * frameW, r * frameH, frameW, frameH);
                    m_tigerFrames[r][c] = QPixmap::fromImage(sub);
                }
            }
            qDebug() << "信息: 成功加载并切片 tiger_new.png 为" << rows << "x" << cols << "帧";
        } else {
            qDebug() << "警告: tiger_new.png 大小异常，跳过切片";
        }
    } else {
        qDebug() << "信息: 未找到 tiger_new.png，继续使用旧的 tiger.png/tiger_man.png 作为回退";
    }
    // 加载三张草贴图
    m_grassTextures[0].load(":/images/grass_0.png");
    if (m_grassTextures[0].isNull()) {
        qDebug() << "警告: 草贴图 grass_0.png 加载失败";
    }
    m_grassTextures[1].load(":/images/grass_1.png");
    if (m_grassTextures[1].isNull()) {
        qDebug() << "警告: 草贴图 grass_1.png 加载失败";
    }

    // 兼容旧的资源配置：如果三张变体都没被打包到资源中，尝试加载旧的单张草贴图作为回退
    bool anyValid = false;
    for (int i = 0; i < 3; ++i) {
        if (!m_grassTextures[i].isNull()) { anyValid = true; break; }
    }
    if (!anyValid) {
        m_grassTextures[0].load(":/images/grass.png");
        if (!m_grassTextures[0].isNull()) {
            qDebug() << "信息: 使用回退草贴图 :/images/grass.png";
        }
    }
    m_grassTextures[2].load(":/images/grass_2.png");
    if (m_grassTextures[2].isNull()) {
        qDebug() << "警告: 草贴图 grass_2.png 加载失败";
    }
    // 加载树的三种装饰贴图
    m_treeTextures[0].load(":/images/tree1.png");
    m_treeTextures[1].load(":/images/tree2.png");
    m_treeTextures[2].load(":/images/tree3.png");
    bool anyTree = false;
    for (int i = 0; i < 3; ++i) if (!m_treeTextures[i].isNull()) anyTree = true;
    if (!anyTree) {
        qDebug() << "信息: 未找到 tree images, 装饰树将不可见";
    }

    // 尝试加载用户提供的地形 atlas（优先资源路径，然后回退到文件系统）
    m_riverAtlas.load(":/images/terrain/river.jpg");
    if (m_riverAtlas.isNull()) {
        // 尝试相对项目路径（运行时可能需要调整）
        QString fsPath = QString("resources/images/terrain/river.jpg");
        m_riverAtlas.load(fsPath);
    }
    if (m_riverAtlas.isNull()) {
        qDebug() << "信息: 未找到 river atlas (: /resources/images/terrain/river.jpg)，河流将以纯色渲染";
    }

    // --- 新增：尝试加载用户提供的生物群系配色图（resources/images/color/<name>.png） ---
    // 对每个 BiomeType 尝试多种命名约定与扩展名（优先 Qt 资源路径，然后文件系统）
    auto tryLoadCandidate = [&](const QString& candidate) -> QPixmap {
        QPixmap pm;
        // 试资源路径
        QString rsrc = QString(":/images/color/%1").arg(candidate);
        pm.load(rsrc);
        if (!pm.isNull()) return pm;
        // 试文件系统路径
        QString fs1 = QString("resources/images/color/%1").arg(candidate);
        pm.load(fs1);
        if (!pm.isNull()) return pm;
        // 试带扩展名 png/jpg/jpeg
        for (const QString& ext : {"png", "jpg", "jpeg"}) {
            QString r2 = rsrc + "." + ext;
            pm.load(r2);
            if (!pm.isNull()) return pm;
            QString f2 = fs1 + "." + ext;
            pm.load(f2);
            if (!pm.isNull()) return pm;
        }
        return QPixmap();
    };

    auto pushBiomePixmap = [&](int biomeInt, const QString& baseName){
        QPixmap loaded;
        // candidate variants: exact, lower, underscore split, hyphen
        std::vector<QString> candidates;
        candidates.push_back(baseName);
        QString lower = baseName.toLower();
        candidates.push_back(lower);
        // insert underscore before capitals -> e.g. "PolarIce" -> "polar_ice"
        QString underscored;
        for (int i=0;i<baseName.size();++i){
            QChar c = baseName[i];
            if (i>0 && c.isUpper()) underscored.push_back('_');
            underscored.push_back(c.toLower());
        }
        candidates.push_back(underscored);
        candidates.push_back(underscored.replace('_', '-'));
        candidates.push_back(lower.replace(' ', '_'));

        for (const QString& cand : candidates) {
            loaded = tryLoadCandidate(cand);
            if (!loaded.isNull()) {
                m_biomePixmaps[biomeInt] = loaded;
                qDebug() << "信息: 为生物群系加载贴图:" << cand << "(biome=" << biomeInt << ")";
                return;
            }
        }
        qDebug() << "信息: 未找到生物群系贴图 (biome=" << biomeInt << ")，将使用颜色回退";
    };

    // 枚举所有 BiomeType（手动列举以避免依赖反射）
    pushBiomePixmap(static_cast<int>(BiomeType::PolarIce), "PolarIce");
    pushBiomePixmap(static_cast<int>(BiomeType::Tundra), "Tundra");
    pushBiomePixmap(static_cast<int>(BiomeType::BorealForest), "BorealForest");
    pushBiomePixmap(static_cast<int>(BiomeType::TemperateForest), "TemperateForest");
    pushBiomePixmap(static_cast<int>(BiomeType::TemperateRainforest), "TemperateRainforest");
    pushBiomePixmap(static_cast<int>(BiomeType::Grassland), "Grassland");
    pushBiomePixmap(static_cast<int>(BiomeType::Savanna), "Savanna");
    pushBiomePixmap(static_cast<int>(BiomeType::TropicalForest), "TropicalForest");
    pushBiomePixmap(static_cast<int>(BiomeType::Desert), "Desert");
    pushBiomePixmap(static_cast<int>(BiomeType::Ocean), "Ocean");
}

namespace {
    QString terrainToString(TerrainType t) {
        switch (t) {
            case TerrainType::LAND: return "土地";
            case TerrainType::WATER: return "水";
            case TerrainType::SHALLOW_RIVER: return "浅河";
            case TerrainType::DEEP_RIVER: return "深河";
            case TerrainType::SHALLOW_OCEAN: return "浅海";
            case TerrainType::DEEP_OCEAN: return "深海";
            case TerrainType::SAND: return "沙地";
            case TerrainType::INLAND_SAND: return "内陆沙地";
            case TerrainType::HILLS: return "丘陵";
            case TerrainType::MOUNTAIN: return "山脉";
            default: return "未知";
        }
    }

    QString biomeToString(BiomeType b) {
        switch (b) {
            case BiomeType::PolarIce: return "极地冰盖";
            case BiomeType::Tundra: return "苔原";
            case BiomeType::BorealForest: return "寒温带针叶林";
            case BiomeType::TemperateForest: return "温带森林";
            case BiomeType::TemperateRainforest: return "温带雨林";
            case BiomeType::Grassland: return "草原";
            case BiomeType::Savanna: return "稀树草原";
            case BiomeType::TropicalForest: return "热带雨林";
            case BiomeType::Desert: return "沙漠";
            case BiomeType::Ocean: return "海洋";
            default: return "未知";
        }
    }
}

void SimulationRenderer::render(QPainter& painter,
                                const std::shared_ptr<EcosystemStateData>& data,
                                const CameraController& camera,
                                const std::optional<SelectableEntity>& hovered,
                                const std::optional<SelectableEntity>& selected,
                                bool isInspectMode,
                                bool isGridInspectMode,
                                const std::optional<QPoint>& hoveredGridCoords)
{
    if (!data) return;

    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);
    // 在背景之上绘制地形瓦片（例如河流）
    if (data && data->world_grid) {
        drawTerrainTiles(painter, data, camera);
    }
    if (data->world_grid) {
        const WorldGrid* grid = data->world_grid;
        if (grid->width() > 0 && grid->height() > 0) {
            const QSize screenSize = m_parentWidget->size();
            if (screenSize.width() > 0 && screenSize.height() > 0) {
                const double visibleWorldWidth = data->world_width / camera.getZoomFactor();
                if (visibleWorldWidth > 0.0) {
                    const double screenAspect = static_cast<double>(screenSize.width()) / static_cast<double>(screenSize.height());
                    const double visibleWorldHeight = visibleWorldWidth / screenAspect;
                    const double viewLeft = camera.getViewCenter().x() - visibleWorldWidth / 2.0;
                    const double viewTop = camera.getViewCenter().y() - visibleWorldHeight / 2.0;
                    const double viewRight = viewLeft + visibleWorldWidth;
                    const double viewBottom = viewTop + visibleWorldHeight;

                    constexpr int buffer = 2;
                    int startX = std::max(0, static_cast<int>(std::floor(viewLeft)) - buffer);
                    int endX = std::min(grid->width() - 1, static_cast<int>(std::ceil(viewRight)) + buffer);
                    int startY = std::max(0, static_cast<int>(std::floor(viewTop)) - buffer);
                    int endY = std::min(grid->height() - 1, static_cast<int>(std::ceil(viewBottom)) + buffer);

                    if (startX <= endX && startY <= endY) {
                        painter.setPen(Qt::NoPen);
                        constexpr double kBrightnessRange = 1.0 - 0.1;

                        for (int y = startY; y <= endY; ++y) {
                            for (int x = startX; x <= endX; ++x) {
                                const Tile& tile = grid->get_tile(x, y);
                                const double darkness = 1.0 - tile.brightness;
                                if (darkness <= 0.0) {
                                    continue;
                                }

                                int alpha = static_cast<int>((darkness / kBrightnessRange) * 160.0);
                                alpha = std::clamp(alpha, 0, 255);
                                if (alpha <= 0) {
                                    continue;
                                }

                                const QPointF screenTopLeft = camera.toScreenCoords(QPointF(static_cast<double>(x), static_cast<double>(y)), screenSize);
                                const QPointF screenBottomRight = camera.toScreenCoords(QPointF(static_cast<double>(x + 1), static_cast<double>(y + 1)), screenSize);
                                painter.setBrush(QColor(0, 0, 30, alpha));
                                painter.drawRect(QRectF(screenTopLeft, screenBottomRight));
                            }
                        }
                    }
                }
            }
        }
    }
    if (m_parentWidget->isGridEnabled()) {
        drawGrid(painter, camera, data->world_width, data->world_height);
    }
    drawEntities(painter, data, camera);
    if (isInspectMode) {
        drawSelection(painter, camera, hovered, selected);
    } else if (isGridInspectMode && hoveredGridCoords.has_value()) {
        drawGridInspect(painter, data, camera, hoveredGridCoords.value());
    }
    drawHud(painter);
}

void SimulationRenderer::drawBackground(QPainter& painter)
{
    // 绘制基础背景
    if (!m_backgroundImage.isNull()) {
        painter.drawPixmap(m_parentWidget->rect(), m_backgroundImage);
    } else {
        painter.fillRect(m_parentWidget->rect(), QColor(34, 139, 34)); // 回退方案
    }
}

void SimulationRenderer::drawEntities(QPainter& painter, const std::shared_ptr<EcosystemStateData>& data, const CameraController& camera)
{
    // ========== 步骤2: 收集、排序并绘制所有生物 ==========

    // --- 核心优化：计算视野内的世界坐标矩形 ---
    const double visibleWorldWidth = data->world_width / camera.getZoomFactor();
    const double screenAspect = (double)m_parentWidget->width() / (double)m_parentWidget->height();
    const double visibleWorldHeight = visibleWorldWidth / screenAspect;
    const double viewLeft = camera.getViewCenter().x() - visibleWorldWidth / 2.0;
    const double viewTop = camera.getViewCenter().y() - visibleWorldHeight / 2.0;
    
    // 创建一个代表视野的矩形，并增加一些缓冲区域，防止边缘物体被错误剔除
    const double buffer = 200.0; // 缓冲的世界单位
    QRectF visibleWorldRect(viewLeft - buffer, viewTop - buffer, visibleWorldWidth + buffer * 2, visibleWorldHeight + buffer * 2);
    // --- 优化结束 ---

    std::vector<DrawableEntity> entitiesToDraw;
    entitiesToDraw.reserve(m_parentWidget->m_grassCount + m_parentWidget->m_cowCount + m_parentWidget->m_tigerCount); // 预分配内存以提高效率

    const double pixelsPerWorldUnit = m_parentWidget->width() / visibleWorldWidth;
    const double animalWorldSize = 3.0; 
    const double animalSizeOnScreen = animalWorldSize * pixelsPerWorldUnit;

    // 循环 1: 收集 Races (动物)
    for (const auto& [name, individuals] : data->race_lists) {
        for (const auto& individual_base : individuals) {
            if (!individual_base || !individual_base->alive) continue;
            if (!visibleWorldRect.contains(individual_base->position.x, individual_base->position.y)) continue;

            const QPixmap* texture = nullptr;
            
            if (name == "cow") {
                auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
                texture = (animal_ptr && animal_ptr->sex == Sex::MALE) ? &m_bullTexture : &m_cowTexture;
            } else if (name == "tiger") {
                // 如果加载了切片，则使用动画帧，否则回退到静态纹理
                auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
                if (!m_tigerFrames.empty()) {
                    // per-instance 动画状态机
                    const RaceBase* key = individual_base.get();
                    auto& state = m_tigerAnimStates[key];
                    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (m_lastUpdateMs == 0) m_lastUpdateMs = nowMs;
                    double dtMs = static_cast<double>(nowMs - m_lastUpdateMs);

                    Position curPos = individual_base->position;
                    if (!state.initialized) {
                        state.last_pos = curPos;
                        state.initialized = true;
                        state.current_frame = 0;
                        state.anim_timer_ms = 0.0;
                        state.direction = 0;
                    }

                    double dx = curPos.x - state.last_pos.x;
                    double dy = curPos.y - state.last_pos.y;
                    double moved = std::sqrt(dx*dx + dy*dy);
                    const double idleThreshold = 0.01; // 世界单位，小到视为静止

                    if (moved < idleThreshold) {
                        // 静止：显示第一帧（idle）
                        state.current_frame = 0;
                        state.anim_timer_ms = 0.0;
                    } else {
                        // 根据 dx,dy 的符号映射到用户提供的4个朝向（行）
                        // 用户说明行从上到下分别是：左下(0), 右下(1), 右上(2), 左上(3)
                        int dir = 0;
                        if (dx >= 0 && dy >= 0) dir = 1; // 右下
                        else if (dx < 0 && dy >= 0) dir = 0; // 左下
                        else if (dx >= 0 && dy < 0) dir = 2; // 右上
                        else if (dx < 0 && dy < 0) dir = 3; // 左上
                        state.direction = dir;

                        // 推进帧计时器
                        state.anim_timer_ms += dtMs;
                        if (state.anim_timer_ms >= m_tigerFrameIntervalMs) {
                            int steps = static_cast<int>(state.anim_timer_ms / m_tigerFrameIntervalMs);
                            state.current_frame = (state.current_frame + steps) % m_tigerFramesPerDir;
                            state.anim_timer_ms -= steps * m_tigerFrameIntervalMs;
                        }
                    }

                    // 选择对应帧
                    int dirIndex = std::clamp(state.direction, 0, m_tigerDirections - 1);
                    int frameIndex = state.current_frame % m_tigerFramesPerDir;
                    texture = &m_tigerFrames[dirIndex][frameIndex];

                    // 更新 last_pos 与 last_seen
                    state.last_pos = curPos;
                    state.last_seen_ms = nowMs;
                } else {
                    texture = (animal_ptr && animal_ptr->sex == Sex::MALE) ? &m_tigerManTexture : &m_tigerTexture;
                }
            }

            if (!texture || texture->isNull()) continue;

            QPointF screenPos = camera.toScreenCoords(QPointF(individual_base->position.x, individual_base->position.y), m_parentWidget->size());
            double widthOnScreen = animalSizeOnScreen;
            double heightOnScreen = animalSizeOnScreen;
            if (name == "tiger") {
                heightOnScreen = animalSizeOnScreen * 0.7;
            }
            QRectF targetRectF(screenPos.x() - widthOnScreen / 2, screenPos.y() - heightOnScreen / 2, widthOnScreen, heightOnScreen);

            entitiesToDraw.push_back({texture, targetRectF.toRect(), individual_base->position.y});

            // --- 新增：绘制血条和能量条 ---

            auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
            if (animal_ptr) {
                if (m_parentWidget->m_showHpBar){
                    double hpPercent = animal_ptr->hp_max > 0 ? animal_ptr->hp_current / animal_ptr->hp_max : 0.0;
                    double energyPercent = animal_ptr->max_energy > 0 ? animal_ptr->energy / animal_ptr->max_energy : 0.0;
                    int barWidth = static_cast<int>(widthOnScreen);
                    int barHeight = std::clamp(static_cast<int>(heightOnScreen * 0.08), 1, 6); // 高度随缩放变化，最小2像素，最大6像素
                    int barX = static_cast<int>(screenPos.x() - barWidth / 2);
                    int hpBarY = static_cast<int>(screenPos.y() - heightOnScreen / 2 - barHeight - 2); // 血条在图片上方
                    int energyBarY = hpBarY + barHeight + 2; // 能量条在血条下方

                    // 血条底色
                    painter.setBrush(QColor(80, 80, 80, 180));
                    painter.setPen(Qt::NoPen);
                    painter.drawRect(barX, hpBarY, barWidth, barHeight);
                    // 血条值
                    painter.setBrush(QColor(220, 20, 60, 220)); // 红色
                    painter.drawRect(barX, hpBarY, static_cast<int>(barWidth * hpPercent), barHeight);

                    // 能量条底色
                    painter.setBrush(QColor(80, 80, 80, 180));
                    painter.drawRect(barX, energyBarY, barWidth, barHeight);
                    // 能量条值
                    painter.setBrush(QColor(30, 144, 255, 220)); // 蓝色
                    painter.drawRect(barX, energyBarY, static_cast<int>(barWidth * energyPercent), barHeight);
                }
            }
        }
    }

    // 循环 2: 收集 Things (植物)
    for (const auto& [name, individuals] : data->thing_lists) {
        if (name == "grass") {
            // 确保至少一张草贴图可用
            bool hasValid = false;
            for (int i = 0; i < 3; ++i) if (!m_grassTextures[i].isNull()) { hasValid = true; break; }
            if (!hasValid) continue;

            const double grassWorldSize = 3.0;
            const double size = grassWorldSize * pixelsPerWorldUnit;

            for (const auto& individual : individuals) {
                if (!individual || !individual->alive) continue;

                // --- 视野剔除 ---
                if (!visibleWorldRect.contains(individual->position.x, individual->position.y)) {
                    continue;
                }

                // 使用后端分配的 variant_index（若无效则回退到伪随机或0）
                int variant = -1;
                if (individual->variant_index >= 0 && individual->variant_index < 3) {
                    variant = individual->variant_index;
                } else {
                    // 回退策略：基于位置的哈希，保证稳定性
                    int posHash = static_cast<int>(individual->position.x * 73856093) ^ 
                                  static_cast<int>(individual->position.y * 19349663);
                    variant = std::abs(posHash) % 3;
                }

                const QPixmap* tex = &m_grassTextures[variant];
                if (tex->isNull()) {
                    for (int i = 0; i < 3; ++i) if (!m_grassTextures[i].isNull()) { tex = &m_grassTextures[i]; break; }
                }

                QPointF screenPos = camera.toScreenCoords(QPointF(individual->position.x, individual->position.y), m_parentWidget->size());
                QRectF targetRectF(screenPos.x() - size / 2, screenPos.y() - size / 2, size, size);
                entitiesToDraw.push_back({tex, targetRectF.toRect(), individual->position.y});
            }
        }
        else if (name == "decor_tree") {
            // 装饰树渲染：使用三张贴图的变体索引
            const double treeWorldSize = 10.0; // 世界单位尺寸（已缩小）
            const double size = treeWorldSize * pixelsPerWorldUnit;

            // 预缩放缓存：按目标像素大小生成 m_treeScaled
            int desiredPixels = static_cast<int>(std::round(size));
            if (desiredPixels > 0) {
                if (m_treeScaledSize != desiredPixels || m_treeScaled.size() != 3) {
                    m_treeScaled.clear();
                    m_treeScaled.resize(3);
                    for (int ti = 0; ti < 3; ++ti) {
                        if (!m_treeTextures[ti].isNull()) {
                            m_treeScaled[ti] = m_treeTextures[ti].scaled(desiredPixels, desiredPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        }
                    }
                    m_treeScaledSize = desiredPixels;
                }
            }

            for (const auto& individual : individuals) {
                if (!individual || !individual->alive) continue;
                if (!visibleWorldRect.contains(individual->position.x, individual->position.y)) continue;

                int variant = 0;
                if (individual->variant_index >= 0 && individual->variant_index < 3) variant = individual->variant_index;
                else {
                    int posHash = static_cast<int>(individual->position.x * 73856093) ^ static_cast<int>(individual->position.y * 19349663);
                    variant = std::abs(posHash) % 3;
                }
                const QPixmap* tex = nullptr;
                if (desiredPixels > 0 && m_treeScaled.size() == 3 && !m_treeScaled[variant].isNull()) {
                    tex = &m_treeScaled[variant];
                } else {
                    tex = &m_treeTextures[variant];
                }
                if (tex->isNull()) continue;

                QPointF screenPos = camera.toScreenCoords(QPointF(individual->position.x, individual->position.y), m_parentWidget->size());
                // 树底对齐：将图片底部对准 position
                QRectF targetRectF(screenPos.x() - size / 2, screenPos.y() - size, size, size);
                entitiesToDraw.push_back({tex, targetRectF.toRect(), individual->position.y});
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

    // 更新 m_lastUpdateMs（放在绘制结束后，以便上面用到的 dtMs 合理）
    m_lastUpdateMs = QDateTime::currentMSecsSinceEpoch();

    // 清理已移除/不在列表中的老虎动画状态，避免无限增长
    if (!m_tigerAnimStates.empty()) {
        std::unordered_set<const RaceBase*> aliveTigers;
        auto it = data->race_lists.find("tiger");
        if (it != data->race_lists.end()) {
            for (const auto& r : it->second) {
                aliveTigers.insert(r.get());
            }
        }
        // 移除 map 中 key 不在 aliveTigers 的条目
        std::vector<const RaceBase*> toErase;
        toErase.reserve(m_tigerAnimStates.size());
        for (const auto& kv : m_tigerAnimStates) {
            if (aliveTigers.find(kv.first) == aliveTigers.end()) toErase.push_back(kv.first);
        }
        for (const auto& k : toErase) m_tigerAnimStates.erase(k);
    }
}

void SimulationRenderer::drawSelection(QPainter& painter, const CameraController& camera, const std::optional<SelectableEntity>& hovered, const std::optional<SelectableEntity>& selected)
{
    // --- 新增：绘制高亮和选中效果 ---
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 绘制悬停高亮
    if (hovered.has_value()) {
        std::visit([&](auto&& arg) {
            QPointF screenPos = camera.toScreenCoords(QPointF(arg->position.x, arg->position.y), m_parentWidget->size());
            painter.setPen(QPen(QColor(255, 255, 0, 200), 3)); // 黄色光圈
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screenPos, 35, 35);
        }, hovered.value());
    }

    // 绘制选中效果和信息框
    if (selected.has_value()) {
        // 绘制选中标记
        std::visit([&](auto&& arg) {
            QPointF screenPos = camera.toScreenCoords(QPointF(arg->position.x, arg->position.y), m_parentWidget->size());
            painter.setPen(QPen(QColor(0, 255, 255, 220), 4)); // 青色光圈
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screenPos, 40, 40);
        }, selected.value());

        // 绘制信息框
        drawSelectionInfo(painter, camera, selected.value());
    }
}

void SimulationRenderer::drawHud(QPainter& painter)
{
    // ========== 步骤3: 绘制信息面板 ==========
    QRectF infoRect(10, 10, 280, 208);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(infoRect, 5, 5);
    
    painter.setPen(Qt::white);
    QFont font("Arial", 12, QFont::Bold);
    painter.setFont(font);
    
    int textY = 30;
    int lineHeight = 24;
    
    painter.drawText(20, textY, QString("年: %1   天: %2").arg(m_parentWidget->m_currentYear).arg(m_parentWidget->m_currentDay));
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("季: %1").arg(QString::fromStdString(m_parentWidget->m_currentQuadrumName)));
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("时间步: %1").arg(m_parentWidget->m_timeStep));
    textY += lineHeight;
    
    painter.drawText(20, textY, QString("模拟 TPS: %1").arg(QString::number(m_parentWidget->m_current_tps, 'f', 1)));
    textY += lineHeight;
    
    int totalCount = m_parentWidget->m_grassCount + m_parentWidget->m_cowCount + m_parentWidget->m_tigerCount;
    painter.drawText(20, textY, QString("总数量: %1").arg(totalCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "草: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("grass"));
    painter.drawText(95, textY, QString::number(m_parentWidget->m_grassCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "牛: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("cow"));
    painter.drawText(95, textY, QString::number(m_parentWidget->m_cowCount));
    textY += lineHeight;
    
    painter.drawText(20, textY, "老虎: ");
    painter.fillRect(70, textY - 14, 18, 18, getColorForName("tiger"));
    painter.drawText(95, textY, QString::number(m_parentWidget->m_tigerCount));

    // ========== 步骤4: 绘制右上角时间 ==========
    {
        QString timeString = QString("%1:%2")
                                 .arg(m_parentWidget->m_currentHour, 2, 10, QChar('0'))
                                 .arg(m_parentWidget->m_currentMinute, 2, 10, QChar('0'));

        QFont timeFont("Arial", 16, QFont::Bold);
        painter.setFont(timeFont);
        painter.setPen(Qt::white);

        QFontMetrics fm(timeFont);
        int textWidth = fm.horizontalAdvance(timeString);
        int margin = 15;
        int x = m_parentWidget->width() - textWidth - margin;
        int y = 35;

        painter.setPen(QColor(0, 0, 0, 120));
        painter.drawText(x + 2, y + 2, timeString);
        painter.setPen(Qt::white);
        painter.drawText(x, y, timeString);
    }
}

void SimulationRenderer::drawSelectionInfo(QPainter& painter, const CameraController& camera, const SelectableEntity& entity)
{
    QString infoText;
    QPointF screenPos;

    // 使用 std::visit 从 variant 中提取信息
    std::visit([&](auto&& arg) {
        screenPos = camera.toScreenCoords(QPointF(arg->position.x, arg->position.y), m_parentWidget->size());

        // 基础信息（所有实体共有）
        infoText += QString("物种: %1\n").arg(QString::fromStdString(arg->species_name));
        infoText += QString("年龄: %1\n").arg(arg->age);
        infoText += QString("能量: %1 / %2").arg(QString::number(arg->energy, 'f', 1)).arg(arg->max_energy);

        // 运行时类型检查：尝试将 RaceBase/ThingBase 转为 Animal
        auto animal_ptr = std::dynamic_pointer_cast<Animal>(arg);
        if (animal_ptr) {
            infoText += QString("\n性别: %1").arg(animal_ptr->sex == Sex::MALE ? "雄性" : "雌性");
#ifdef ECOSIM_ENABLE_UI_DEBUG
            AnimalUiSnapshot ui = animal_ptr->get_ui_snapshot();
            std::string status = ui.current_bt_action;
            if (ui.is_pregnant) {
                status += " (Pregnant)";
            }
            infoText += QString("\n状态: %1").arg(QString::fromStdString(status));

            // 显示当前移速（每 tick 步长）
            infoText += QString("\n移速: %1").arg(QString::number(ui.current_speed, 'f', 2));

            // 显示生命值（HP）
            infoText += QString("\n生命: %1 / %2")
                .arg(QString::number(ui.hp_current, 'f', 0))
                .arg(QString::number(ui.hp_max, 'f', 0));

            QString hungerStr = "普通";
            if (ui.hunger_state == 0) {
                hungerStr = "饱足";
            } else if (ui.hunger_state == 2) {
                hungerStr = "饥饿";
            }
            infoText += QString("\n饥饿: %1").arg(hungerStr);

            infoText += QString("\n感知: %1食物, %2配偶")
                .arg(ui.perceived_food)
                .arg(ui.perceived_mates);
            if (ui.danger_nearby > 0) {
                infoText += " (有威胁!)";
            }

            if (ui.current_bt_action == "Wandering" && ui.wander_total_ticks > 0) {
                infoText += QString("\n游荡: %1 / %2")
                    .arg(ui.wander_current_ticks)
                    .arg(ui.wander_total_ticks);
            }else{
                infoText += QString("\n游荡: Unknown");
            }

            if (m_parentWidget->getShowHistory()) {
                infoText += "\n--- 历史记录 (最近5条) ---";
                if (ui.interaction_history.empty()) {
                    infoText += "\n(无)";
                } else {
                    int count = 0;
                    for (auto it = ui.interaction_history.rbegin();
                         it != ui.interaction_history.rend() && count < 5;
                         ++it, ++count) {
                        infoText += QString("\n[T:%1] %2 (%3)")
                            .arg(it->timestamp)
                            .arg(QString::fromStdString(it->message))
                            .arg(it->success ? "OK" : "Fail");
                    }
                }
            }

            // 绘制寻路叠加：当前目标与规划路径（仅 UI Debug）
            if (ui.current_target.has_value()) {
                QPointF targetPos = camera.toScreenCoords(QPointF(ui.current_target->x, ui.current_target->y), m_parentWidget->size());
                painter.setPen(QPen(QColor(0, 200, 255, 200), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(targetPos, 10, 10);
                painter.drawLine(QPointF(targetPos.x() - 12, targetPos.y()), QPointF(targetPos.x() + 12, targetPos.y()));
                painter.drawLine(QPointF(targetPos.x(), targetPos.y() - 12), QPointF(targetPos.x(), targetPos.y() + 12));
            }

            if (!ui.planned_path.empty()) {
                QPen pathPen(QColor(0, 255, 0, 200)); // <-- 修改为绿色 (Green)
                pathPen.setWidth(2);
                pathPen.setStyle(Qt::SolidLine); // <-- 修改为实线
                painter.setPen(pathPen);
                QPointF prev = camera.toScreenCoords(QPointF(animal_ptr->position.x, animal_ptr->position.y), m_parentWidget->size());
                for (const auto& p : ui.planned_path) {
                    QPointF sp = camera.toScreenCoords(QPointF(p.x, p.y), m_parentWidget->size());
                    painter.drawLine(prev, sp);
                    prev = sp;
                }
            }
#endif // ECOSIM_ENABLE_UI_DEBUG
        }

        // 为植物显示繁殖积累能量
        auto producer_ptr = std::dynamic_pointer_cast<Producer>(arg);
        if (producer_ptr) {
            infoText += QString("\n繁殖积累: %1 / %2")
                .arg(QString::number(producer_ptr->reproduction_energy_accumulated, 'f', 1))
                .arg(QString::number(producer_ptr->repro_energy_threshold, 'f', 1));
        }
    }, entity);

    // 计算绘制位置
    QFont font("Arial", 10);
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(QRect(), Qt::AlignLeft, infoText);
    textRect.adjust(-10, -10, 10, 10); // 添加内边距
    textRect.moveTo(screenPos.x() + 40, screenPos.y() - textRect.height() / 2); // 移动到目标右侧

    // 确保不超出屏幕边界
    if (textRect.right() > m_parentWidget->width()) textRect.moveRight(m_parentWidget->width() - 10);
    if (textRect.left() < 0) textRect.moveLeft(10);
    if (textRect.bottom() > m_parentWidget->height()) textRect.moveBottom(m_parentWidget->height() - 10);
    if (textRect.top() < 0) textRect.moveTop(10);

    // 绘制半透明背景和文本
    painter.setBrush(QColor(0, 0, 0, 190));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(textRect, 5, 5);

    painter.setPen(Qt::white);
    painter.setFont(font);
    painter.drawText(textRect, Qt::AlignCenter, infoText);
}

QColor SimulationRenderer::getColorForName(const std::string& name) const
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

void SimulationRenderer::drawGrid(QPainter& painter, const CameraController& camera, double worldWidth, double worldHeight)
{
    if (worldWidth <= 0 || worldHeight <= 0) return;

    const QSize screenSize = m_parentWidget->size();
    const double visibleWorldWidth = worldWidth / camera.getZoomFactor();
    const double screenAspect = static_cast<double>(screenSize.width()) / static_cast<double>(screenSize.height());
    const double visibleWorldHeight = visibleWorldWidth / screenAspect;
    const double viewLeft = camera.getViewCenter().x() - visibleWorldWidth / 2.0;
    const double viewTop = camera.getViewCenter().y() - visibleWorldHeight / 2.0;
    const double viewRight = viewLeft + visibleWorldWidth;
    const double viewBottom = viewTop + visibleWorldHeight;

    int startX = static_cast<int>(std::floor(std::max(0.0, viewLeft)));
    int endX   = static_cast<int>(std::ceil(std::min(worldWidth, viewRight)));
    int startY = static_cast<int>(static_cast<int>(std::floor(std::max(0.0, viewTop))));
    int endY   = static_cast<int>(std::ceil(std::min(worldHeight, viewBottom)));

    QPen thinPen(QColor(255, 255, 255, 70));
    thinPen.setWidth(1);
    painter.setPen(thinPen);

    // 垂直网格线
    for (int x = startX; x <= endX; ++x) {
        QPointF top = camera.toScreenCoords(QPointF(x, viewTop), screenSize);
        QPointF bottom = camera.toScreenCoords(QPointF(x, viewBottom), screenSize);
        painter.drawLine(QLineF(top, bottom));
    }

    // 水平网格线
    for (int y = startY; y <= endY; ++y) {
        QPointF left = camera.toScreenCoords(QPointF(viewLeft, y), screenSize);
        QPointF right = camera.toScreenCoords(QPointF(viewRight, y), screenSize);
        painter.drawLine(QLineF(left, right));
    }
}

void SimulationRenderer::drawGridInspect(QPainter& painter,
                                         const std::shared_ptr<EcosystemStateData>& data,
                                         const CameraController& camera,
                                         const QPoint& gridCoords)
{
    QPointF worldTopLeft(gridCoords.x(), gridCoords.y());
    QPointF worldBottomRight(gridCoords.x() + 1.0, gridCoords.y() + 1.0);

    QPointF screenTopLeft = camera.toScreenCoords(worldTopLeft, m_parentWidget->size());
    QPointF screenBottomRight = camera.toScreenCoords(worldBottomRight, m_parentWidget->size());

    QRectF highlightRect(screenTopLeft, screenBottomRight);
    painter.setBrush(QColor(255, 255, 0, 70));
    painter.setPen(QPen(QColor(255, 255, 0, 200), 2));
    painter.drawRect(highlightRect);

    if (!data->world_grid) {
        qWarning() << "Renderer: data->world_grid is null";
        return;
    }

    const WorldGrid* grid = data->world_grid;
    if (!grid->is_valid_coord(gridCoords.x(), gridCoords.y())) {
        return;
    }

    const Tile& tile = grid->get_tile(gridCoords.x(), gridCoords.y());

    QString infoText;
    infoText += QString("格子坐标: (%1, %2)\n").arg(gridCoords.x()).arg(gridCoords.y());
    // 显示经纬度
    infoText += QString("纬度: %1°\n").arg(QString::number(tile.latitude, 'f', 2));
    infoText += QString("经度: %1°\n").arg(QString::number(tile.longitude, 'f', 2));
    infoText += QString("地形: %1\n").arg(terrainToString(tile.terrain));
    infoText += QString("生物群系: %1\n").arg(biomeToString(tile.biome));
    infoText += QString("----------\n");
    infoText += QString("温度: %1 °C\n").arg(QString::number(tile.temperature, 'f', 1));
    infoText += QString("本地时间: %1:00\n").arg(tile.local_hour);
    infoText += QString("湿度: %1\n").arg(QString::number(tile.moisture, 'f', 2));
    infoText += QString("海拔: %1\n").arg(QString::number(tile.elevation, 'f', 2));
    infoText += QString("肥沃度: %1\n").arg(tile.fertility);
    infoText += QString("亮度: %1\n").arg(tile.brightness);
    infoText += QString("物体数量: %1").arg(tile.things.size());

    QFont font("Arial", 10);
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(QRect(), Qt::AlignLeft, infoText);
    textRect.adjust(-10, -10, 10, 10);

    QPointF cursorPos = m_parentWidget->mapFromGlobal(QCursor::pos());
    textRect.moveTo(cursorPos.x() + 20.0, cursorPos.y() + 20.0);

    if (textRect.right() > m_parentWidget->width()) textRect.moveRight(m_parentWidget->width() - 10);
    if (textRect.bottom() > m_parentWidget->height()) textRect.moveBottom(m_parentWidget->height() - 10);
    if (textRect.left() < 0) textRect.moveLeft(10);
    if (textRect.top() < 0) textRect.moveTop(10);

    painter.setBrush(QColor(0, 0, 0, 190));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(textRect, 5, 5);

    painter.setPen(Qt::white);
    painter.setFont(font);
    painter.drawText(textRect.adjusted(10, 10, -10, -10), Qt::AlignLeft, infoText);
}

void SimulationRenderer::drawTerrainTiles(QPainter& painter,
                                         const std::shared_ptr<EcosystemStateData>& data,
                                         const CameraController& camera)
{
    if (!data || !data->world_grid) return;
    const WorldGrid* grid = data->world_grid;
    const QSize screenSize = m_parentWidget->size();

    const double visibleWorldWidth = data->world_width / camera.getZoomFactor();
    const double screenAspect = static_cast<double>(screenSize.width()) / static_cast<double>(screenSize.height());
    const double visibleWorldHeight = visibleWorldWidth / screenAspect;
    const double viewLeft = camera.getViewCenter().x() - visibleWorldWidth / 2.0;
    const double viewTop = camera.getViewCenter().y() - visibleWorldHeight / 2.0;
    const double viewRight = viewLeft + visibleWorldWidth;
    const double viewBottom = viewTop + visibleWorldHeight;

    constexpr int buffer = 2;
    int startX = std::max(0, static_cast<int>(std::floor(viewLeft)) - buffer);
    int endX = std::min(grid->width() - 1, static_cast<int>(std::ceil(viewRight)) + buffer);
    int startY = std::max(0, static_cast<int>(std::floor(viewTop)) - buffer);
    int endY = std::min(grid->height() - 1, static_cast<int>(std::ceil(viewBottom)) + buffer);

    if (startX > endX || startY > endY) return;

    bool haveAtlas = !m_riverAtlas.isNull();
    int atlasCols = std::max(1, m_riverAtlasCols);
    int atlasRows = std::max(1, m_riverAtlasRows);
    int cellW = haveAtlas ? (m_riverAtlas.width() / atlasCols) : 0;
    int cellH = haveAtlas ? (m_riverAtlas.height() / atlasRows) : 0;

    // --- 新增: 首先绘制生物群系基底（使用图片回退到纯色） ---
    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            const Tile& tile = grid->get_tile(x, y);

            QPointF screenTopLeft = camera.toScreenCoords(QPointF(static_cast<double>(x), static_cast<double>(y)), screenSize);
            QPointF screenBottomRight = camera.toScreenCoords(QPointF(static_cast<double>(x + 1), static_cast<double>(y + 1)), screenSize);
            QRectF destRect(screenTopLeft, screenBottomRight);

            // 尝试使用预加载的群系贴图
            auto it = m_biomePixmaps.find(static_cast<int>(tile.biome));
            if (it != m_biomePixmaps.end() && !it->second.isNull()) {
                // 绘制并拉伸贴图以覆盖整个格子
                // QPainter 没有接受 (QRectF, QPixmap) 的重载，使用带 sourceRect 的重载
                QRectF srcRect(0.0, 0.0, static_cast<qreal>(it->second.width()), static_cast<qreal>(it->second.height()));
                painter.drawPixmap(destRect, it->second, srcRect);
            } else {
                // 回退：按群系类型选择基本颜色并考虑亮度
                QColor baseColor;
                switch (tile.biome) {
                    case BiomeType::PolarIce: baseColor = QColor(240, 250, 250); break;
                    case BiomeType::Tundra: baseColor = QColor(200, 220, 200); break;
                    case BiomeType::BorealForest: baseColor = QColor(100, 140, 100); break;
                    case BiomeType::TemperateForest: baseColor = QColor(80, 160, 90); break;
                    case BiomeType::TemperateRainforest: baseColor = QColor(40, 120, 60); break;
                    case BiomeType::Grassland: baseColor = QColor(170, 210, 120); break;
                    case BiomeType::Savanna: baseColor = QColor(200, 190, 120); break;
                    case BiomeType::TropicalForest: baseColor = QColor(40, 140, 70); break;
                    case BiomeType::Desert: baseColor = QColor(230, 210, 150); break;
                    case BiomeType::Ocean: baseColor = QColor(30, 100, 180); break;
                    default: baseColor = QColor(120, 120, 120); break;
                }
                // 考虑 tile.brightness（0..1）来调整颜色亮度
                double b = std::clamp(tile.brightness, 0.0, 1.0);
                int r = static_cast<int>(baseColor.red() * b + 10 * (1.0 - b));
                int g = static_cast<int>(baseColor.green() * b + 10 * (1.0 - b));
                int bl = static_cast<int>(baseColor.blue() * b + 10 * (1.0 - b));
                painter.fillRect(destRect, QColor(r, g, bl));
            }
        }
    }

    // --- 然后绘制河流覆盖（保留原有河流 atlas 或纯色渲染） ---
    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            const Tile& tile = grid->get_tile(x, y);
            if (tile.terrain != TerrainType::SHALLOW_RIVER && tile.terrain != TerrainType::DEEP_RIVER) continue;

            QPointF screenTopLeft = camera.toScreenCoords(QPointF(static_cast<double>(x), static_cast<double>(y)), screenSize);
            QPointF screenBottomRight = camera.toScreenCoords(QPointF(static_cast<double>(x + 1), static_cast<double>(y + 1)), screenSize);
            QRectF destRect(screenTopLeft, screenBottomRight);

            if (haveAtlas && cellW > 0 && cellH > 0) {
                int col = ((x % atlasCols) + atlasCols) % atlasCols;
                int row = ((y % atlasRows) + atlasRows) % atlasRows;
                QRect srcRect(col * cellW, row * cellH, cellW, cellH);
                painter.drawPixmap(destRect, m_riverAtlas, srcRect);
            } else {
                // 没有 atlas 的回退：用蓝色渐层简单表示河流
                QColor riverColor = (tile.terrain == TerrainType::DEEP_RIVER) ? QColor(20, 50, 180) : QColor(50, 120, 220);
                painter.fillRect(destRect, riverColor);
            }
        }
    }
}