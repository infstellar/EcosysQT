#include "MainWindow.h"
#include "StartScreenWidget.h"
#include "Widget.h"
#include "simulation.h"
#include "ecosystem.h"
#include <QDebug> // 用于输出日志
#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QUrl>
#include "map_config_loader.h"
#include <spdlog/spdlog.h>
#include "SaveManager.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_isMusicPlaying(false)
    , m_forceCreateFromConfig(false) // <-- 初始化新字段
{
    // 1. 只读取 map 配置用于窗口尺寸/日志，不在构造时创建后端控制器
    EcosystemConfig config = load_map_config_from_yaml("config/map_config.yaml");
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[UI] Loaded map config: {}x{} ({} species counts)",
                       config.world_width, config.world_height, config.initial_populations.size());

    // 延迟创建 controller 与 simulation widget，避免程序启动时就初始化种群
    m_controller = nullptr;

    // 2. 创建开始界面（延迟创建模拟界面）
    m_startScreen = new StartScreenWidget(this);
    m_simulationWidget = nullptr; // 延迟创建

    // 3. 创建 QStackedWidget 并仅添加开始界面
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->addWidget(m_startScreen);

    // 4. 将 QStackedWidget 设置为中央控件
    setCentralWidget(m_stackedWidget);
    resize(config.world_width, config.world_height); // 根据配置设置窗口大小
    setWindowTitle("Ecosystem Simulation");

    // 5. 连接信号和槽，实现界面切换（仅把 startClicked 连接到 loadOrNewSimulation）
    connect(m_startScreen, &StartScreenWidget::startClicked, this, &MainWindow::loadOrNewSimulation);
    connect(m_startScreen, &StartScreenWidget::exitClicked, this, &MainWindow::exitApplication);
    connect(m_startScreen, &StartScreenWidget::toggleMusicClicked, this, &MainWindow::onToggleMusic);

    // --- 新增：初始化背景音乐播放器 ---
    m_backgroundMusic = new QMediaPlayer(this);
    QMediaPlaylist *playlist = new QMediaPlaylist(this);
    playlist->addMedia(QUrl("qrc:/music/background_music.mp3"));
    playlist->setPlaybackMode(QMediaPlaylist::Loop);
    m_backgroundMusic->setPlaylist(playlist);
    m_backgroundMusic->setVolume(50); // 设置一个合适的音量 (0-100)
    //m_backgroundMusic->play();
}

MainWindow::~MainWindow()
{
    if (m_controller) {
        qDebug() << "Stopping simulation thread...";
        m_controller->stop();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 直接关闭程序时不弹出保存对话（按用户要求）
    event->accept();
}

void MainWindow::onToggleMusic(bool play)
{
    if (play && !m_isMusicPlaying) {
        m_backgroundMusic->play();
        m_isMusicPlaying = true;
        qDebug() << "Music enabled";
    } else if (!play && m_isMusicPlaying) {
        m_backgroundMusic->stop();
        m_isMusicPlaying = false;
        qDebug() << "Music disabled";
    }
}

QString MainWindow::selectSaveSlot()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QDir exeQDir(exeDir);
    const QString savesDir = exeQDir.filePath(QStringLiteral("saves"));
    if (!QDir(savesDir).exists()) {
        bool ok = QDir().mkpath(savesDir);
        qDebug() << "Creating saves directory:" << savesDir << " result:" << ok;
    }

    // 固定三个存档槽
    QStringList fileNames;
    fileNames << QDir(savesDir).filePath(QStringLiteral("save1.yaml"))
              << QDir(savesDir).filePath(QStringLiteral("save2.yaml"))
              << QDir(savesDir).filePath(QStringLiteral("save3.yaml"));

    // 构造显示列表：包含每个槽的状态信息
    QStringList displayList;
    for (int i = 0; i < fileNames.size(); ++i) {
        QFileInfo fi(fileNames[i]);
        QString label = QStringLiteral("Save Slot %1").arg(i + 1);
        if (fi.exists()) {
            QString mod = fi.lastModified().toString("yyyy-MM-dd HH:mm:ss");
            label += QStringLiteral(" - 修改: %1").arg(mod);
        } else {
            label += QStringLiteral(" - (空)");
        }
        displayList << label;
    }

    // 在列表末尾加入 新建 存档 选项
    const QString kNewSaveLabel = QStringLiteral("Create New Save...");
    displayList << kNewSaveLabel;

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this,
        QStringLiteral("Select Save"),
        QStringLiteral("Please select a save slot or create a new one:"),
        displayList,
        0,
        false,
        &ok
    );
    if (!ok || selected.isEmpty()) return QString();

    // 用户选择新建
    if (selected == kNewSaveLabel) {
        // 让用户在三个槽中选择要创建/覆盖的目标槽
        QStringList slotLabels;
        for (int i = 0; i < fileNames.size(); ++i) {
            QFileInfo fi(fileNames[i]);
            QString lab = QStringLiteral("Save Slot %1").arg(i + 1);
            if (fi.exists()) lab += QStringLiteral(" (已存在)");
            slotLabels << lab;
        }

        bool ok2 = false;
        QString chosenSlot = QInputDialog::getItem(
            this,
            QStringLiteral("Choose Target Slot"),
            QStringLiteral("Select a save slot to overwrite:"),
            slotLabels,
            0,
            false,
            &ok2
        );
        if (!ok2 || chosenSlot.isEmpty()) {
            return QString(); // 取消创建
        }

        int idx = slotLabels.indexOf(chosenSlot);
        if (idx < 0 || idx >= fileNames.size()) return QString();

        // 如果目标槽已有文件，提示覆盖确认
        QFileInfo targetFi(fileNames[idx]);
        if (targetFi.exists()) {
            auto rb = QMessageBox::question(
                this,
                QStringLiteral("Confirm Overwrite"),
                QStringLiteral("The selected save slot already exists. Overwrite?"),
                QMessageBox::Yes | QMessageBox::No
            );
            if (rb != QMessageBox::Yes) {
                return QString(); // 用户放弃覆盖
            }
        }

        // 标记：下一次加载要按 map_config 创建（reset），并记录目标槽路径
        m_forceCreateFromConfig = true;
        m_pendingSaveFile = fileNames[idx];

        // 返回目标槽路径（loadOrNewSimulation 会根据 m_forceCreateFromConfig 执行 reset）
        return fileNames[idx];
    }

    // 用户选择已有槽，返回对应路径
    int idx = displayList.indexOf(selected);
    if (idx >= 0 && idx < fileNames.size()) {
        // 清除新建标记（选择现有槽表示非“新建”行为）
        m_forceCreateFromConfig = false;
        m_pendingSaveFile.clear();
        return fileNames[idx];
    }

    return QString();
}

void MainWindow::loadOrNewSimulation()
{
    QString saveFile = selectSaveSlot();
    if (saveFile.isEmpty()) {
        qDebug() << "No save selected; cancelling simulation start";
        return;
    }

    // 若用户之前选择了“新建”并且目标槽与当前 saveFile 相同，强制按 map_config 创建（reset）
    bool forceCreateNow = false;
    if (m_forceCreateFromConfig && !m_pendingSaveFile.isEmpty() && m_pendingSaveFile == saveFile) {
        forceCreateNow = true;
        // 立即清除标记（只作用一次）
        m_forceCreateFromConfig = false;
        m_pendingSaveFile.clear();
    }

    // --- 先尝试在创建 controller 之前读取存档内容（避免时序和初始化冲突） ---
    std::shared_ptr<EcosystemStateData> loadedData = nullptr;
    if (!forceCreateNow && QFileInfo(saveFile).exists()) {
        loadedData = SaveManager::loadFromYaml(saveFile);
        if (!loadedData) {
            qDebug() << "loadOrNewSimulation: 存档存在但未能解析或为空，将新建存档：" << saveFile;
        } else {
            qDebug() << "loadOrNewSimulation: 成功解析存档：" << saveFile;
        }
    } else {
        if (forceCreateNow) {
            qDebug() << "loadOrNewSimulation: 按 map_config 强制新建并覆盖存档槽：" << saveFile;
        } else {
            qDebug() << "loadOrNewSimulation: 存档文件不存在，将使用新建配置：" << saveFile;
        }
    }

    // 如果已有 controller，先停止并销毁，保证可重复加载不同存档
    if (m_controller) {
        m_controller->stop();
        m_controller.reset();
    }

    EcosystemConfig config = load_map_config_from_yaml("config/map_config.yaml");
    // 创建 controller（注意：SimulationEngine 在构造时会初始化一次，但我们随后会用 loadFromSnapshot 或 reset 覆盖）
    m_controller = std::make_unique<SimulationController>(config);

    if (loadedData) {
        // 加载快照（loadFromSnapshot 内部会把数据写入 engine）
        m_controller->loadFromSnapshot(loadedData);
    } else {
        // 无可用存档或强制新建：使用配置重置（初始化）
        m_controller->reset(config);

        // 如果这是用户在“新建”时指定的槽，保存刚创建的初始快照到该槽以覆盖文件
        if (forceCreateNow) {
            auto initData = m_controller->get_data();
            if (initData) {
                bool saved = SaveManager::saveToYaml(initData, saveFile);
                if (!saved) {
                    qWarning() << "Failed to write save file after creation:" << saveFile;
                } else {
                    qDebug() << "Initial save generated from map_config written to:" << saveFile;
                }
            } else {
                qWarning() << "Unable to obtain initial data for save after creation";
            }
        }
    }

    // 创建或重建 Widget：确保 addWidget 一次且连接正确
    if (!m_simulationWidget) {
        m_simulationWidget = new Widget(m_controller.get(), this);
        m_stackedWidget->addWidget(m_simulationWidget);
        connect(m_simulationWidget, &Widget::exitToStartScreen, this, &MainWindow::showStartScreen);
    } else {
        // 移除并销毁旧的 widget，再创建新的并加入 stacked
        m_stackedWidget->removeWidget(m_simulationWidget);
        disconnect(m_simulationWidget, nullptr, this, nullptr);
        delete m_simulationWidget;
        m_simulationWidget = new Widget(m_controller.get(), this);
        m_stackedWidget->addWidget(m_simulationWidget); // 必须 add 回去
        connect(m_simulationWidget, &Widget::exitToStartScreen, this, &MainWindow::showStartScreen);
    }

    m_controller->set_target_fps(30);
    showSimulationScreen();
}

bool MainWindow::trySaveOnExit() {
    // 弹出三选一：是/否/取消。取消会中止返回开始界面（并恢复模拟）
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Game", "Save current progress?", QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
    if (reply == QMessageBox::Cancel) {
        return false; // 取消：不要切换回开始界面
    }
    if (reply == QMessageBox::No) {
        return true; // 不保存，继续返回开始界面
    }
    // Yes -> 保存
    QString saveFile = selectSaveSlot();
    if (saveFile.isEmpty()) {
        // 用户在选择存档时取消，则视为取消整个返回动作
        return false;
    }
    auto data = m_controller ? m_controller->get_data() : nullptr;
    if (!data) {
        qWarning() << "No data to save (get_data returned null)";
        QMessageBox::warning(this, "Save Failed", "No data available to save.");
        return false;
    }
    bool ok = SaveManager::saveToYaml(data, saveFile);
    if (!ok) {
        QMessageBox::warning(this, "Save Failed", "Failed to write save file. Check disk permissions.");
        return false;
    }
    return true;
}

void MainWindow::showSimulationScreen()
{
    qDebug() << "Switching to simulation view and starting simulation...";
    if (m_controller) {
        // 确保 widget 已加入 stacked，再启动后端
        if (!m_simulationWidget) {
            qWarning() << "showSimulationScreen: 模拟界面为空，无法切换";
            return;
        }
        m_stackedWidget->setCurrentWidget(m_simulationWidget);
        m_controller->start(); // 启动模拟线程（放在切换后，避免时序问题）
    } else {
        qWarning() << "showSimulationScreen: 控制器为空，无法启动模拟";
    }
}

void MainWindow::showStartScreen()
{
    qDebug() << "Returning to start screen; stopping simulation...";

    if (!m_controller) {
        m_stackedWidget->setCurrentWidget(m_startScreen);
        return;
    }

    // 先立即停止后端，保证后端不再继续运行
    m_controller->stop();

    // 弹出保存对话；如果用户取消（返回 false），则重启后端并留在模拟界面
    bool proceed = trySaveOnExit();
    if (!proceed) {
        qDebug() << "User cancelled return; restarting simulation thread...";
        m_controller->start(); // 恢复运行
        return; // 保持在模拟界面
    }

    // 用户选择继续返回开始界面（无论是否保存），停止后端已完成
    m_stackedWidget->setCurrentWidget(m_startScreen);
}

void MainWindow::exitApplication() {
    // 从开始界面直接退出程序时不保存
    qDebug() << "Exiting application...";
    close();
}