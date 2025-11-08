#include "StartScreenWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>

StartScreenWidget::StartScreenWidget(QWidget *parent) : QWidget(parent)
{
    // 设置背景色
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(25, 25, 40));
    setPalette(pal);

    // 标题
    QLabel* titleLabel = new QLabel("生态系统模拟", this);
    QFont titleFont("Arial", 40, QFont::Bold);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("color: white;");
    titleLabel->setAlignment(Qt::AlignCenter);

    // 创建按钮
    m_startButton = new QPushButton("开始模拟", this);
    m_exitButton = new QPushButton("退出程序", this);

    // 设置按钮样式
    QString buttonStyle = "QPushButton { background-color: #007ACC; color: white; border: none; padding: 15px; font-size: 18px; border-radius: 5px; min-width: 200px; } QPushButton:hover { background-color: #005A9E; }";
    m_startButton->setStyleSheet(buttonStyle);
    m_exitButton->setStyleSheet(buttonStyle);
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_exitButton->setCursor(Qt::PointingHandCursor);

    // 布局
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(20);
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_exitButton);
    buttonLayout->setAlignment(Qt::AlignCenter);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addStretch();
    mainLayout->addWidget(titleLabel);
    mainLayout->addSpacing(50);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    
    setLayout(mainLayout);

    // 连接信号：点击按钮时，发出我们自定义的信号
    connect(m_startButton, &QPushButton::clicked, this, &StartScreenWidget::startClicked);
    connect(m_exitButton, &QPushButton::clicked, this, &StartScreenWidget::exitClicked);
}