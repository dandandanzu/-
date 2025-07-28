#ifndef PORT_H
#define PORT_H

#include <QDialog>
#include <QSerialPortInfo>
#include <QSerialPort>

namespace Ui {
class port;
}

class port : public QDialog
{
    Q_OBJECT

public:
    explicit port(QWidget *parent = nullptr);
    ~port();

    QSerialPort serial;
    bool serialOpen = false;  // 串口状态标志

private slots:
    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

private:
    Ui::port *ui;
};

#endif // PORT_H
