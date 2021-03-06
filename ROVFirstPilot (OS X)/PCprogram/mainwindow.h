#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCamera>
#include <QCameraInfo>
#include <QCameraViewfinder>
#include <QPixmap>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    void setpalprop();
    QCamera *camera;
    QCamera *camera2;
    QPixmap *red;
    QPixmap *green;
    QPalette *okauto;
    QPalette *noauto;
protected:
    void paintEvent(QPaintEvent *);
private:
    Ui::MainWindow *ui;
public slots:
    void onupdateInfo(int cam2id, int depth, int speed, int yaw, int pitch, int roll,int setyaw,int setpitch, bool connection, bool ayaw, bool apitch, bool adepth, bool leak);
    void replugJoy(bool trig);
    void replugCams(bool trig);
    void openDebugWindow(bool trig);
signals:
    void updateJoy();

};

#endif // MAINWINDOW_H
