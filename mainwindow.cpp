#include "mainwindow.h"
#include "ui_mainwindow.h" // 这个文件由 CMAKE_AUTOUIC 自动生成
#include "Widget.h"
#include "DummyBackend.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 移除模板中的演示标签，避免显示 "Hello, World from VS Code!"
    if (ui->label) {
        ui->verticalLayout->removeWidget(ui->label);
        ui->label->deleteLater();
    }

    // 初始化演示后端并将自定义 Widget 添加到布局
    m_backend = std::make_unique<DummyBackend>();
    m_widget = new Widget(m_backend.get(), this);
    ui->verticalLayout->addWidget(m_widget);
}

MainWindow::~MainWindow()
{
    delete ui;
}