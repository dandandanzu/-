#include "port.h"
#include "ui_port.h"
#include <QMessageBox>

port::port(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::port)
{       
    // 去掉右上角的 "？" 按钮
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    this->setWindowTitle("串口设置");
    ui->setupUi(this);

    on_pushButton_clicked();
}

port::~port()
{
    delete ui;
}

// 刷新串口
void port::on_pushButton_clicked()
{
    // 清除所有串口
    ui->comboBox->clear();

    // 遍历电脑串口
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        QString text = QString("%1 (%2)").arg(info.portName(), info.description());
        ui->comboBox->addItem(text, info.portName()); // portName 存储在 UserRole
    }
}

// 打开串口
void port::on_pushButton_2_clicked()
{
    if (!serialOpen) {
        // 设置你要连接的串口名，例如从 ComboBox 获取
        QString portName = ui->comboBox->currentData().toString();  // 假设你用了 comboBox 选择串口

        serial.setPortName(portName);

        // 未打开，尝试打开串口
        if (serial.isOpen()) {
            serial.close();  // 保险起见，先关掉
        }

        // 先尝试 open 看是否已被占用
        if (!serial.open(QIODevice::ReadWrite)) {
            QMessageBox::critical(this, "错误", QString("串口 %1 已被占用或无法打开").arg(portName));
            return;
        }

        // 设置参数（如果 open 成功）
        serial.setBaudRate(QSerialPort::Baud9600);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);

        serialOpen = true;
        ui->pushButton_2->setText("关闭串口");
        ui->pushButton_2->setStyleSheet("background-color: #00CC66; color: white;");
    }
    else {
        serial.close();
        serialOpen = false;
        ui->pushButton_2->setText("打开串口");
        ui->pushButton_2->setStyleSheet("");
    }
}

