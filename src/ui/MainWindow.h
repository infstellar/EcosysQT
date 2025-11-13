#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <memory>
#include <QString>

// 前向声明
class SimulationController;
class Widget;
class StartScreenWidget;
class QMediaPlayer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void onToggleMusic(bool play);

private slots:
    void showSimulationScreen();
    void showStartScreen();
    void exitApplication();
    QString selectSaveSlot();
    bool trySaveOnExit();
    void loadOrNewSimulation();

private:
    QStackedWidget *m_stackedWidget;
    StartScreenWidget *m_startScreen;
    Widget *m_simulationWidget;
    QMediaPlayer* m_backgroundMusic;
    bool m_isMusicPlaying;
    std::unique_ptr<SimulationController> m_controller;

    // 新增：当用户在 UI 选择“新建存档”时，标记下一次加载按 map_config 创建（reset）
    bool m_forceCreateFromConfig = false;
    QString m_pendingSaveFile; // 用户选择的新建槽路径

protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // MAINWINDOW_H