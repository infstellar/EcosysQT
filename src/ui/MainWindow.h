#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <memory>

// 前向声明
class SimulationController;
class Widget;
class StartScreenWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void showSimulationScreen();
    void showStartScreen();
    void exitApplication();

private:
    QStackedWidget *m_stackedWidget;
    StartScreenWidget *m_startScreen;
    Widget *m_simulationWidget;

    // 将 Controller 的所有权移到主窗口中
    std::unique_ptr<SimulationController> m_controller;
};

#endif // MAINWINDOW_H