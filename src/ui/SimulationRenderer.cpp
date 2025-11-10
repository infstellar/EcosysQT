#include "SimulationRenderer.h"
#include "CameraController.h"
#include "animal.h"
#include "thing_base.h"
#include "race_base.h"
#ifdef ECOSIM_ENABLE_UI_DEBUG
#include "animal_ui_snapshot.h"
#endif
#include <QDebug>
#include <algorithm>

// DrawableEntity 结构体只在渲染时使用，所以定义在这里
struct DrawableEntity {
    const QPixmap* texture;
    QRect targetRect;
    double worldY; // 用于排序
};

SimulationRenderer::SimulationRenderer(Widget* parentWidget) : m_parentWidget(parentWidget)
{
    // --- 新增：加载背景和生物贴图 ---
    m_backgroundImage.load(":/images/background.png");
    if (m_backgroundImage.isNull()) {
        qDebug() << "警告: 背景图加载失败，使用纯色背景";
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
    m_grassTexture.load(":/images/grass.png");
    if (m_grassTexture.isNull()) {
        qDebug() << "警告: 草贴图加载失败";
    }
}

void SimulationRenderer::render(QPainter& painter,
                                const std::shared_ptr<EcosystemStateData>& data,
                                const CameraController& camera,
                                const std::optional<SelectableEntity>& hovered,
                                const std::optional<SelectableEntity>& selected,
                                bool isInspectMode)
{
    if (!data) return;

    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);
    drawEntities(painter, data, camera);
    if (isInspectMode) {
        drawSelection(painter, camera, hovered, selected);
    }
    drawHud(painter);
}

void SimulationRenderer::drawBackground(QPainter& painter)
{
    // ========== 步骤1: 绘制背景和时间遮罩 ==========
    // 1.1 首先绘制基础背景图
    if (!m_backgroundImage.isNull()) {
        painter.drawPixmap(m_parentWidget->rect(), m_backgroundImage);
    } else {
        painter.fillRect(m_parentWidget->rect(), QColor(34, 139, 34)); // 回退方案
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

        const int currentHour = m_parentWidget->m_currentHour;

        if (currentHour >= nightStart || currentHour < dawnStart) {
            // --- 夜晚 (22:00 - 03:59) ---
            alpha = nightAlpha;
        } else if (currentHour >= duskStart) {
            // --- 黄昏 (18:00 - 21:59) ---
            // 透明度从 0 (18:00) 线性增加到 nightAlpha (22:00)
            double progress = static_cast<double>(currentHour - duskStart) / (nightStart - duskStart);
            alpha = static_cast<int>(progress * nightAlpha);
        } else if (currentHour >= dayStart) {
            // --- 白天 (08:00 - 17:59) ---
            alpha = 0; // 完全明亮，无遮罩
        } else if (currentHour >= dawnStart) {
            // --- 黎明 (04:00 - 07:59) ---
            // 透明度从 nightAlpha (04:00) 线性减少到 0 (08:00)
            double progress = static_cast<double>(currentHour - dawnStart) / (dayStart - dawnStart);
            alpha = static_cast<int>((1.0 - progress) * nightAlpha);
        }

        // 限制 alpha 在有效范围内
        alpha = std::clamp(alpha, 0, 255);

        // 绘制遮罩
        if (alpha > 0) {
            painter.fillRect(m_parentWidget->rect(), QColor(0, 0, 30, alpha)); // 使用深蓝色调的遮罩，效果更自然
        }
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
    const double animalWorldSize = 100.0; 
    const double animalSizeOnScreen = animalWorldSize * pixelsPerWorldUnit;

    // 循环 1: 收集 Races (动物)
    for (const auto& [name, individuals] : data->race_lists) {
        for (const auto& individual_base : individuals) {
            if (!individual_base || !individual_base->alive) continue;

            // --- 核心优化：视野剔除 ---
            if (!visibleWorldRect.contains(individual_base->position.x, individual_base->position.y)) {
                continue;
            }
            // --- 优化结束 ---

            const QPixmap* texture = nullptr;
            
            if (name == "cow") {
                auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
                texture = (animal_ptr && animal_ptr->sex == Sex::MALE) ? &m_bullTexture : &m_cowTexture;
            } else if (name == "tiger") {
                auto animal_ptr = std::dynamic_pointer_cast<Animal>(individual_base);
                texture = (animal_ptr && animal_ptr->sex == Sex::MALE) ? &m_tigerManTexture : &m_tigerTexture;
            }

            if (!texture || texture->isNull()) continue;

            QPointF screenPos = camera.toScreenCoords(QPointF(individual_base->position.x, individual_base->position.y), m_parentWidget->size());
            
            const double size = animalSizeOnScreen;
            QRectF targetRectF(screenPos.x() - size / 2, screenPos.y() - size / 2, size, size);
            
            entitiesToDraw.push_back({texture, targetRectF.toRect(), individual_base->position.y});
        }
    }

    // 循环 2: 收集 Things (植物)
    for (const auto& [name, individuals] : data->thing_lists) {
        if (name == "grass") {
            if (m_grassTexture.isNull()) continue;
            
            const double grassWorldSize = 100.0;
            const double size = grassWorldSize * pixelsPerWorldUnit;
            
            for (const auto& individual : individuals) {
                if (!individual || !individual->alive) continue;

                // --- 核心优化：视野剔除 ---
                if (!visibleWorldRect.contains(individual->position.x, individual->position.y)) {
                    continue;
                }
                // --- 优化结束 ---

                QPointF screenPos = camera.toScreenCoords(QPointF(individual->position.x, individual->position.y), m_parentWidget->size());
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
#endif // ECOSIM_ENABLE_UI_DEBUG
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