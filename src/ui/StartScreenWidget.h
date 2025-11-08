#ifndef STARTSCREENWIDGET_H
#define STARTSCREENWIDGET_H

#include <QWidget>
#include <QPushButton>

class StartScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StartScreenWidget(QWidget *parent = nullptr);

signals:
    void startClicked();
    void exitClicked();

private:
    QPushButton *m_startButton;
    QPushButton *m_exitButton;
};

#endif // STARTSCREENWIDGET_H