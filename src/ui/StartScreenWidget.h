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
    void toggleMusicClicked(bool play);

private slots:
    void onMusicButtonClicked();

private:
    QPushButton *m_startButton;
    QPushButton *m_exitButton;
    QPushButton *m_musicButton;
    bool m_isMusicOn;
};

#endif // STARTSCREENWIDGET_H