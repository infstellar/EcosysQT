#include "MainWindow.h"
#include "StartScreenWidget.h"
#include "Widget.h"
#include "simulation.h"
#include "ecosystem.h"
#include <QDebug> // 用于输出日志

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 1. 创建后端控制器 (这是程序中唯一创建 Controller 的地方)
    EcosystemConfig config(1600, 900); 
    config.initial_populations = {
        {"grass", 100000},
        {"cow", 3},
        {"tiger", 3},
    };
    m_controller = std::make_unique<SimulationController>(config);

    // 2. 创建各个界面
    m_startScreen = new StartScreenWidget(this);
    m_simulationWidget = new Widget(m_controller.get(), this); // 将控制器指针传给模拟界面

    // 3. 创建 QStackedWidget 并添加界面
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->addWidget(m_startScreen);
    m_stackedWidget->addWidget(m_simulationWidget);

    // 4. 将 QStackedWidget 设置为中央控件
    setCentralWidget(m_stackedWidget);
    resize(1600, 900); // 设置一个合适的窗口大小
    setWindowTitle("生态系统模拟");

    // 5. 连接信号和槽，实现界面切换
    connect(m_startScreen, &StartScreenWidget::startClicked, this, &MainWindow::showSimulationScreen);
    connect(m_startScreen, &StartScreenWidget::exitClicked, this, &MainWindow::exitApplication);
    connect(m_simulationWidget, &Widget::exitToStartScreen, this, &MainWindow::showStartScreen);
}

MainWindow::~MainWindow()
{
    // 析构函数在程序关闭时被调用，确保模拟线程被停止
    if (m_controller) {
        qDebug() << "正在停止模拟线程...";
        m_controller->stop();
    }
}

void MainWindow::showSimulationScreen()
{
    qDebug() << "开始模拟...";
    m_controller->start(); // 在这里启动或重置模拟
    m_stackedWidget->setCurrentWidget(m_simulationWidget);
}

void MainWindow::showStartScreen()
{
    qDebug() << "返回开始界面，停止模拟...";
    m_controller->stop(); // 返回开始界面时，停止模拟
    m_stackedWidget->setCurrentWidget(m_startScreen);
}

void MainWindow::exitApplication()
{
    qDebug() << "退出程序...";
    close(); // 关闭主窗口，这将触发析构函数并最终退出 app.exec()
}