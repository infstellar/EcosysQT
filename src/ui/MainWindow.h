#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <memory>

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

public slots: // <-- 将槽函数设为 public，以便连接
    void onToggleMusic(bool play); // <-- 新增：控制音乐播放的槽

private slots:
    void showSimulationScreen();
    void showStartScreen();
    void exitApplication();

private:
    QStackedWidget *m_stackedWidget;
    StartScreenWidget *m_startScreen;
    Widget *m_simulationWidget;
    QMediaPlayer* m_backgroundMusic; // <-- 新增：音乐播放器
    bool m_isMusicPlaying; // <-- 新增：跟踪音乐状态
    // 将 Controller 的所有权移到主窗口中
    std::unique_ptr<SimulationController> m_controller;
};

#endif // MAINWINDOW_H