#ifndef STARTSCREENWIDGET_H
#define STARTSCREENWIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QPixmap>
class StartScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StartScreenWidget(QWidget *parent = nullptr);

signals:
    void startClicked();
    void exitClicked();
    void toggleMusicClicked(bool play);

protected: 
    void paintEvent(QPaintEvent *event) override; 

private slots:
    void onMusicButtonClicked();

private:
    QPushButton *m_startButton;
    QPushButton *m_exitButton;
    QPushButton *m_musicButton;
    QPixmap m_backgroundImage;
    bool m_isMusicOn;
};

#endif // STARTSCREENWIDGET_H