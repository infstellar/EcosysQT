#include "StartScreenWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>

StartScreenWidget::StartScreenWidget(QWidget *parent) 
    : QWidget(parent)
    , m_isMusicOn(true) // 按钮状态默认开启
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
    m_musicButton = new QPushButton("关闭音乐", this); // <-- 新增：创建音乐按钮

    // 设置按钮样式
    QString buttonStyle = "QPushButton { background-color: #007ACC; color: white; border: none; padding: 15px; font-size: 18px; border-radius: 5px; min-width: 200px; } QPushButton:hover { background-color: #005A9E; }";
    m_musicButton->setStyleSheet(buttonStyle);
    m_startButton->setStyleSheet(buttonStyle);
    m_exitButton->setStyleSheet(buttonStyle);
    m_startButton->setCursor(Qt::PointingHandCursor);
    m_exitButton->setCursor(Qt::PointingHandCursor);

    // 布局
    QVBoxLayout *buttonLayout = new QVBoxLayout();
    buttonLayout->setSpacing(20);
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(m_exitButton);
    buttonLayout->addWidget(m_musicButton);
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
    connect(m_musicButton, &QPushButton::clicked, this, &StartScreenWidget::onMusicButtonClicked); // <-- 新增：连接音乐按钮
}

// --- 新增：实现音乐按钮点击槽函数 ---
void StartScreenWidget::onMusicButtonClicked()
{
    m_isMusicOn = !m_isMusicOn; // 切换状态
    if (m_isMusicOn) {
        m_musicButton->setText("关闭音乐");
    } else {
        m_musicButton->setText("开启音乐");
    }
    emit toggleMusicClicked(m_isMusicOn); // 发射信号，通知主窗口
}