#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QDebug>
#include <QTime>
#include <QScrollBar>
#include <QScreen>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QTableWidgetItem>
#include "crc.h"

#define SecondByte_06   0x06
#define LengthByte_00   0x00
#define LengthByte_01   0x01

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    serialRefreshInit();        // 刷新全部串口
    interfaceInit();        // 界面初始化
    //regInit();      // 寄存器信息显示
    pushButtonInit();       // 按钮状态初始化
    connect(&sensorSerial, &QSerialPort::readyRead, this, &MainWindow::sensorSerialDelay);      // 传感器串口接收槽函数

    // 万用表串口接收槽函数
    connect(&MultimeterSerial, &QSerialPort::readyRead, this, &MainWindow::multimeterSerialDelay);
    connect(&MultimeterSerialDelayTimer, &QTimer::timeout, this, &MainWindow::multimeterSerialRead);

    // 气压仪串口接收槽函数
    connect(&BarographSerial, &QSerialPort::readyRead, this, &MainWindow::barographSerialRead);

    // 自动扫描槽函数
    connect(&autoScanTimer, &QTimer::timeout, this, &MainWindow::autoScan);

    // 自动出队定时器槽函数
    connect(&autoDequeueTimer, &QTimer::timeout, this, &MainWindow::autoDequeueSolt);

    // 串口接收延时槽函数
    connect(&waitingResponseTimer, &QTimer::timeout, this, &MainWindow::waitingResponseTimerSolt);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 全部串口刷新
void MainWindow::serialRefreshInit()
{
    on_pushButton_clicked();              // 传感器串口刷新
    on_pushButton_3_clicked();            // 万用表串口刷新
    on_pushButton_5_clicked();            // 气压仪串口刷新
}

// 窗口更新,
void MainWindow::appendToTextEdit(SerialPortState state, const QString &address, const QString &value)
{
    switch (state) {
    case Write:
        ui->plainTextEdit->appendPlainText(QString("写->%1->%2").arg(address).arg(value));
            ui->plainTextEdit->moveCursor(QTextCursor::End);
        break;
    case Read:
        ui->plainTextEdit->appendPlainText(QString("读->%1").arg(address));
            ui->plainTextEdit->moveCursor(QTextCursor::End);
        break;
    case Receive:
        ui->plainTextEdit->appendPlainText(QString("收->%1").arg(value));
            ui->plainTextEdit->moveCursor(QTextCursor::End);
        break;
    default:
        break;
    }
}

// 串口读命令
void MainWindow::serialRead(SerialPortID id, QString address, QString length)
{
    switch (id) {
    case Portsensor:
        if (sensorSerial.isOpen()) {
            QByteArray addressBytes = QByteArray::fromHex(address.toUtf8());
            QByteArray lengthBytes = QByteArray::fromHex(length.toUtf8());

            // 电流型设备ID固定为01，电压型可变
            int strID, functionCode;
            functionCode = 04;
            if (!isVoltageSensor) {
                strID = 01;
            } else {
                strID = receDeviceID.toInt();
            }

            // 构造完整帧
            QByteArray frame;
            frame.append(strID);                // 固定头1
            frame.append(functionCode);         // 固定头2
            frame.append(addressBytes[0]);          // 地址尾1
            frame.append(addressBytes[1]);          // 地址尾2
            frame.append(static_cast<char>(LengthByte_00));         // 固定尾1
            frame.append(lengthBytes[0]);           // 动态尾2

            // CRC
            quint16 crc = Modbus_CRC16(reinterpret_cast<uint8_t *>(frame.data()), frame.length());
            frame.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC高字节
            frame.append(static_cast<char>(crc & 0xFF));        // CRC低字节

            // 串口写入
            sensorSerial.write(frame);
            ui->statusbar->showMessage("发送->" + frame.toHex().toUpper());
        } else {
            QMessageBox::critical(this, "错误", "传感器串口未打开");
        }
        break;
    case PortMultimeter:
        if (MultimeterSerial.isOpen()) {
        } else {
            QMessageBox::critical(this, "错误", "万用表串口未打开");
        }
        break;
    case PortBarograph:
        if (BarographSerial.isOpen()) {
        } else {
            QMessageBox::critical(this, "错误", "气压仪串口未打开");
        }
        break;
    default:
        break;
    }
}

// 串口写命令
void MainWindow::serialWrite(SerialPortID id, QString address, QString data)
{
    switch (id) {
    case Portsensor:
        if (sensorSerial.isOpen()) {
            QByteArray addressBytes = QByteArray::fromHex(address.toUtf8());

            // 电流型设备ID固定为01，电压型可变
            int strID;
            if (!isVoltageSensor) {
                strID = 01;
            } else {
                strID = receDeviceID.toInt();
            }

            // 构造完整帧
            QByteArray frame;            

            // 如果写入数据是float型
            if (isFloat) {
                frame.append(strID);                                  // 固定头1
                frame.append(0x10);                                   // 功能码 0x10 写多个寄存器
                frame.append(addressBytes[0]);                        // 高位
                frame.append(addressBytes[1]);                        // 低位
                frame.append(static_cast<char>(LengthByte_00));       // 写入寄存器数量高位
                frame.append(0x02);                                   // 写两个寄存器（float
                frame.append(0x04);                                   // 数据字节数（2个寄存器 = 4字节）

                // float → 4 字节
                float floatData = data.toFloat();                     // 添加数据字节
                uint32_t temp;
                memcpy(&temp, &floatData, 4);
                frame.append((temp >> 24) & 0xFF);
                frame.append((temp >> 16) & 0xFF);
                frame.append((temp >> 8)  & 0xFF);
                frame.append(temp & 0xFF);
                isFloat = false;
            } else {
                bool ok;
                quint16 value = data.toUShort(&ok, 16);
                frame.append(strID);         // 固定头1
                frame.append(static_cast<char>(SecondByte_06));         // 固定头2
                frame.append(addressBytes[0]);      // 地址尾1
                frame.append(addressBytes[1]);      // 地址尾2
                frame.append(static_cast<char>((value >> 8) & 0xFF));         // 数据尾1
                frame.append(static_cast<char>(value & 0xFF));         // 数据尾2
            }

            // CRC
            quint16 crc = Modbus_CRC16(reinterpret_cast<uint8_t *>(frame.data()), frame.length());
            frame.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC高字节
            frame.append(static_cast<char>(crc & 0xFF));        // CRC低字节
            // 串口发送
            sensorSerial.write(frame);
            ui->statusbar->showMessage("发送->" + frame.toHex().toUpper());
            delay(70);      // 主控写flash需要时间，延时70等待主控写入flash
        } else {
            QMessageBox::critical(this, "错误", "传感器串口未打开");
        }
        break;
    case PortMultimeter:
        if (MultimeterSerial.isOpen()) {

            QByteArray dataBytes = data.toUtf8();
            dataBytes.append("\r\n");  // 添加换行符，符合 SCPI 终止符规范
            MultimeterSerial.write(dataBytes);
        } else {
            QMessageBox::critical(this, "错误", "万用表串口未打开");
        }
        break;
    case PortBarograph:
        if (BarographSerial.isOpen()) {
        } else {
            QMessageBox::critical(this, "错误", "气压仪串口未打开");
        }
        break;
    default:
        break;
    }
}

// 传感器串口接收延时
void MainWindow::sensorSerialDelay()
{
    autoDequeueTimer.stop();
    timeoutTimes = 0;
    errValueTimes = 0;
    data.append(sensorSerial.readAll());
    if (data.length() < 5) return;      // 数据最短为差错码，5个字节
    if (static_cast<quint8>(data[1]) == 0x04 || static_cast<quint8>(data[1]) == 0x03) {     // 读功能
        if (static_cast<quint8>(data[2]) == 0x02) {     // 1个寄存器地址 2个字节
            if (data.length() == 7) {
                if (isSetBtn) {
                    isSetBtn = false;
                    ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"收："+QString(data.toHex(' ').toUpper().append(' '))+"\n");
                    ui->plainTextEdit_2->moveCursor(QTextCursor::End);
                }
                QByteArray dataPart = data.left(5);
                quint16 receivedCrc = (static_cast<quint8>(data[5]) << 8) | static_cast<quint8>(data[6]);
                quint16 calculatedCrc = Modbus_CRC16(reinterpret_cast<uint8_t *>(dataPart.data()), dataPart.length());
                if (receivedCrc == calculatedCrc) {
                } else {
                    appendToTextEdit(Receive, "", "CRC校验不通过");
                }
                QString valueStr;       // 用来暂存转换后的值
                // 解析数据
                quint16 value = (static_cast<quint16>(static_cast<quint8>(data[3])) << 8)
                                | static_cast<quint8>(data[4]);
                valueStr = QString::number(value);

                // 调用数据
                if (pressureUnit) {                     // 读取压力单位
                    if (static_cast<quint8>(data[4]) == 0x01) {
                        ui->lineEdit_6->setText("kPa");
                        ui->pushButton_108->setStyleSheet("");
                        ui->pushButton_109->setStyleSheet("background-color: #00BFFF");
                        decimalPoint = 4;
                        pressureUnit = false;
                    } else {
                        ui->lineEdit_6->setText("Pa");
                        ui->pushButton_108->setStyleSheet("background-color: #00BFFF");
                        ui->pushButton_109->setStyleSheet("");
                        decimalPoint = 1;
                        pressureUnit = false;
                    }
                } else if (isTemperature) {             // 读取温度
                    // 转换为10进制并除以10
                    double result = static_cast<double>(value) / 10.0;
                    valueStr = QString::number(result);
                    isTemperature = false;
                    ui->lineEdit_4->setText(valueStr + "℃");
                } else if (isFirmwareVersion) {         // 读取固件版本
                    valueStr = data.mid(3, 2).toHex().toUpper();
                    appendToTextEdit(Receive, "", "固件版本是："+valueStr);
                    isFirmwareVersion = false;
                } else if (LineEditID) {                // 设备ID
                    if (static_cast<quint8>(data[0]) == 0x00) {
                        LineEditID->setText(valueStr);
                        appendToTextEdit(Receive, "", "设备连接成功，设备地址是" + valueStr);
                        receDeviceID = valueStr;
                        LineEditID = nullptr;
                    } else {
                        LineEditID->setText(valueStr);
                        appendToTextEdit(Receive, "", LineEditID->text());
                        LineEditID = nullptr;
                    }
                } else if (ComboBoxBaud) {                  // 波特率
                    if (!isVoltageSensor) {
                        ComboBoxBaud->setCurrentText(baudMap.key(valueStr));
                        appendToTextEdit(Receive, "", baudMap.key(valueStr));
                        ComboBoxBaud = nullptr;
                    } else {
                        ComboBoxBaud->setCurrentText(voltageBaudMap.key(valueStr));
                        appendToTextEdit(Receive, "", voltageBaudMap.key(valueStr));
                        ComboBoxBaud = nullptr;
                    }

                } else if (LineEditCurrentMin) {            // 电流下限
                    LineEditCurrentMin->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentMin = nullptr;
                } else if (LineEditCurrentMax) {            // 电流上限
                    LineEditCurrentMax->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentMax = nullptr;
                } else if (LineEditZeroTracking) {          // 零点跟踪
                    LineEditZeroTracking->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditZeroTracking = nullptr;
                } else if (LineEditCurrentCalibrationNum) {        // 电流标定点数
                    LineEditCurrentCalibrationNum->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibrationNum = nullptr;
                } else if (LineEditCurrentCalibration1) {          // 电流标定点1电流uA
                    LineEditCurrentCalibration1->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibration1 = nullptr;
                } else if (LineEditCurrentCalibration2) {          // 电流标定点2电流uA
                    LineEditCurrentCalibration2->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibration2 = nullptr;
                } else if (LineEditCurrentCalibration3) {          // 电流标定点3电流uA
                    LineEditCurrentCalibration3->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibration3 = nullptr;
                } else if (LineEditCurrentCalibration4) {          // 电流标定点4电流uA
                    LineEditCurrentCalibration4->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibration4 = nullptr;
                } else if (LineEditCurrentCalibration5) {          // 电流标定点5电流uA
                    LineEditCurrentCalibration5->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditCurrentCalibration5 = nullptr;
                } else if (isSensor) {                       // 传感器列表
                    if (!isVoltageSensor) {
                        for (QPushButton *btn : buttonList) {
                            if (btn->text() == sensorListMap.key(valueStr)) {
                                btn->setStyleSheet("background-color: #00BFFF;");
                                break;
                            }
                        }
                        appendToTextEdit(Receive, "", sensorListMap.key(valueStr));
                    } else {
                        for (QPushButton *btn : buttonList) {
                            if (btn->text() == voltageSensorListMap.key(valueStr)) {
                                btn->setStyleSheet("background-color: #00BFFF;");
                                break;
                            }
                        }
                        appendToTextEdit(Receive, "", voltageSensorListMap.key(valueStr));
                    }
                    isSensor = false;
                } else if (LineEditInternalCode) {          // 内码
                    LineEditInternalCode->setText(valueStr);
                    LineEditInternalCode = nullptr;
                } else if (LineEditCurrent) {               // 电流
                    LineEditCurrent->setText(valueStr);
                    LineEditCurrent = nullptr;
                } else if (LineEditPWM) {                   // PWM
                    LineEditPWM->setText(valueStr);
                    LineEditPWM = nullptr;
                } else if (LineEditStartPointuA) {              // 变送起点uA
                    LineEditStartPointuA->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditStartPointuA = nullptr;
                } else if (LineEditEndPointuA) {                // 变送满度uA
                    LineEditEndPointuA->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditEndPointuA = nullptr;
                } else if (isTransmissionMethod) {
                    currentTransmissionMethod(valueStr);
                    appendToTextEdit(Receive, "", "变送方式是 "+transmissionMethodListMap.key(valueStr));
                    isTransmissionMethod = false;
                } else if (LineEditSensorCalibrationNum) {
                    LineEditSensorCalibrationNum->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditSensorCalibrationNum = nullptr;
                } else if (LineEditPoint) {
                    LineEditPoint->setText(valueStr);
                    appendToTextEdit(Receive, "", valueStr);
                    LineEditPoint = nullptr;
                } else if (ComboBoxCheck) {
                    ComboBoxCheck->setCurrentIndex(valueStr.toInt());
                    appendToTextEdit(Receive, "", ComboBoxCheck->currentText());
                    ComboBoxCheck = nullptr;
                }
            } else if (data.length() > 7) {
                appendToTextEdit(Receive, "", "接收数据长度异常");
            } else if (data.length() < 7) return;       // 如果长度不等于7，return，data接着append，直到长度等于7
        } else if (static_cast<quint8>(data[2]) == 0x04) {      // 2个寄存器地址，4个字节
            if (data.length() == 9) {
                if (isSetBtn) {
                    isSetBtn = false;
                    ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"收："+QString(data.toHex(' ').toUpper().append(' '))+"\n");
                    ui->plainTextEdit_2->moveCursor(QTextCursor::End);
                }

                QByteArray dataPart = data.left(7);
                quint16 receivedCrc = (static_cast<quint8>(data[7]) << 8) | static_cast<quint8>(data[8]);
                quint16 calculatedCrc = Modbus_CRC16(reinterpret_cast<uint8_t *>(dataPart.data()), dataPart.length());
                if (receivedCrc == calculatedCrc) {
                } else {
                    appendToTextEdit(Receive, "", "CRC校验不通过");
                }
                QString valueStr;       // 用来暂存转换后的值
                // 解析数据
                uint32_t intValue = 0;
                QByteArray floatBytes = data.mid(3, 4);             // 从第4字节开始取4字节（C4BB8000）
                for (int i = 0; i < 4; ++i) {
                    intValue |= static_cast<quint8>(floatBytes.at(i)) << (8 * (3 - i));
                }
                // 如果是float型，否择是32位int型
                if (isFloat) {                                      //  uint32_t 的二进制表示转为 float
                    float floatValue;
                    memcpy(&floatValue, &intValue, sizeof(float));  // 安全类型转换
                    valueStr = QString::number(floatValue);         // 转换浮点数为字符串
                    isFloat = false;                                // 转换完成后修改状态

                    // ui->lineEdit_46->setText(valueStr);

                    // 转换值发给控件显示
                    if (LineEditOutMinPa) {                         // 最小输出值Pa
                        LineEditOutMinPa->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditOutMinPa = nullptr;
                    } else if (LineEditOutMaxPa) {                  // 最大输出值Pa
                        LineEditOutMaxPa->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditOutMaxPa = nullptr;
                    } else if (LineEditOutMincmH2O) {               // 最小输出值_cmH2O
                        LineEditOutMincmH2O->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditOutMincmH2O = nullptr;
                    } else if (LineEditOutMaxcmH2O) {               // 最大输出值_cmH2O
                        LineEditOutMaxcmH2O->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditOutMaxcmH2O = nullptr;
                    } else if (LineEditStartPointPa) {              // 变送起点Pa
                        LineEditStartPointPa->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditStartPointPa = nullptr;
                    } else if (LineEditEndPointPa) {                // 变送满度Pa
                        LineEditEndPointPa->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditEndPointPa = nullptr;
                    } else if (LineEditPressure) {                  // 压力
                        QString valueStr1 = QString::number(floatValue, 'f', decimalPoint);  // 保留3位小数
                        LineEditPressure->setText(valueStr1);
                        LineEditPressure = nullptr;
                    } else if (LineEditSensorCalibration1) {                  // 读传感器标定点1Pa
                        LineEditSensorCalibration1->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorCalibration1 = nullptr;
                    } else if (LineEditSensorCalibration2) {                  // 读传感器标定点2Pa
                        LineEditSensorCalibration2->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorCalibration2 = nullptr;
                    } else if (LineEditSensorCalibration3) {                  // 读传感器标定点3Pa
                        LineEditSensorCalibration3->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorCalibration3 = nullptr;
                    } else if (LineEditSensorCalibration4) {                  // 读传感器标定点4Pa
                        LineEditSensorCalibration4->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorCalibration4 = nullptr;
                    } else if (LineEditSensorCalibration5) {                  // 读传感器标定点5Pa
                        LineEditSensorCalibration5->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorCalibration5 = nullptr;
                    }
                } else {                                            // uint32_t先直接转化为10进制
                    valueStr = QString::number(intValue);           // 默认就是十进制

                    //ui->lineEdit_46->setText(valueStr);

                    if (isFirmwareinfo) {                           // 固件信息
                        QString firmwareinfoStr = data.mid(3, 4).toHex().toUpper();
                        appendToTextEdit(Receive, "", "固件信息是："+firmwareinfoStr);
                        isFirmwareinfo = false;
                    } else if (LineEditMinInternalCode) {           // 最小内码值
                        LineEditMinInternalCode->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditMinInternalCode = nullptr;
                    } else if (LineEditMaxInternalCode) {           // 满度内码值
                        LineEditMaxInternalCode->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditMaxInternalCode = nullptr;
                    } else if (LineEditInternalCode1) {             // 电流标定点1对应PWM定时器计数值
                        LineEditInternalCode1->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditInternalCode1 = nullptr;
                    } else if (LineEditInternalCode2) {             // 电流标定点2对应PWM定时器计数值
                        LineEditInternalCode2->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditInternalCode2 = nullptr;
                    } else if (LineEditInternalCode3) {             // 电流标定点3对应PWM定时器计数值
                        LineEditInternalCode3->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditInternalCode3 = nullptr;
                    } else if (LineEditInternalCode4) {             // 电流标定点4对应PWM定时器计数值
                        LineEditInternalCode4->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditInternalCode4 = nullptr;
                    } else if (LineEditInternalCode5) {             // 电流标定点5对应PWM定时器计数值
                        LineEditInternalCode5->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditInternalCode5 = nullptr;
                    } else if (LineEditSensorInternalCode1) {             // 传感器标定点1对应内码值
                        LineEditSensorInternalCode1->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorInternalCode1 = nullptr;
                    } else if (LineEditSensorInternalCode2) {             // 传感器标定点2对应内码值
                        LineEditSensorInternalCode2->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorInternalCode2 = nullptr;
                    } else if (LineEditSensorInternalCode3) {             // 传感器标定点3对应内码值
                        LineEditSensorInternalCode3->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorInternalCode3 = nullptr;
                    } else if (LineEditSensorInternalCode4) {             // 传感器标定点4对应内码值
                        LineEditSensorInternalCode4->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorInternalCode4 = nullptr;
                    } else if (LineEditSensorInternalCode5) {             // 传感器标定点5对应内码值
                        LineEditSensorInternalCode5->setText(valueStr);
                        appendToTextEdit(Receive, "", valueStr);
                        LineEditSensorInternalCode5 = nullptr;
                    }
                }
            } else if (data.length() > 9) {
                appendToTextEdit(Receive, "", "接收数据长度异常");
            } else if (data.length() < 9) return;     // 如果长度不等于9，return，data接着append，直到长度等于9
        }
    } else if (static_cast<quint8>(data[1]) == 0x06 || static_cast<quint8>(data[1]) == 0x10) {      // 写单个寄存器地址功能
        if (data.length() < 8) {
            return;     // 如果长度不等于8，return，data接着append，直到长度等于8
        } else if (data.length() == 8) {
            if (isSetBtn) {
                isSetBtn = false;
                ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"收："+QString(data.toHex(' ').toUpper().append(' '))+"\n");
                ui->plainTextEdit_2->moveCursor(QTextCursor::End);
            }

            QByteArray dataPart = data.left(6);
            quint16 receivedCrc = (static_cast<quint8>(data[6]) << 8) | static_cast<quint8>(data[7]);
            quint16 calculatedCrc = Modbus_CRC16(reinterpret_cast<uint8_t *>(dataPart.data()), dataPart.length());
            if (receivedCrc == calculatedCrc) {
            } else {
                appendToTextEdit(Receive, "", "CRC校验不通过");
            }
            appendToTextEdit(Receive, "", "写入成功");
            if (isDeviceID) {
                receDeviceID = QString::number(static_cast<quint8>(data[5]));
                isDeviceID = false;
            } else if (setpressureUnit) {
                functionQueue.dequeue();        // 先把写压力单位的队列清掉
                QTimer::singleShot(100, [=]{
                    if (!isVoltageSensor) {
                        functionQueue.enqueue([this]() {
                            pressureUnit = true;
                            serialRead(Portsensor, "0012", "01");
                        });
                    } else {
                        functionQueue.enqueue([this]() {
                            pressureUnit = true;
                            serialRead(Portsensor, "0002", "01");
                        });
                    }
                });
                setpressureUnit = false;
            }
        } else if (data.length() > 8) {
            appendToTextEdit(Receive, "", "接收数据长度异常");
        }
    } else if (static_cast<quint8>(data[1]) == 0x84 || static_cast<quint8>(data[1]) == 0x85 || static_cast<quint8>(data[1]) == 0x83) {
        if (data.length() == 5) {
            if (isSetBtn) {
                isSetBtn = false;
                ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"收："+QString(data.toHex(' ').toUpper().append(' '))+"\n");
                ui->plainTextEdit_2->moveCursor(QTextCursor::End);
            }

            QByteArray dataPart = data.left(3);
            quint16 receivedCrc = (static_cast<quint8>(data[3]) << 8) | static_cast<quint8>(data[4]);
            quint16 calculatedCrc = Modbus_CRC16(reinterpret_cast<uint8_t *>(dataPart.data()), dataPart.length());
            if (receivedCrc == calculatedCrc) {
            } else {
                appendToTextEdit(Receive, "", "CRC校验不通过");
            }
            appendToTextEdit(Receive, "", "接收到错误码->"+data.toHex().toUpper());
            if (errValueTimes < 5) {
                errValueTimes++;
            } else {
                autoScanTimer.stop();
                isAutoScan = false;         // 这里可能会有问题，有可能 自动扫描需要点两次才会扫描
                ui->pushButton_22->setStyleSheet("");
                ui->pushButton_22->setText("扫描设备");
                delay(10);
                queueClear();
                delay(10);
                errValueTimes = 0;
            }
        } else if (data.length() > 5) {
            appendToTextEdit(Receive, "", "接收数据长度异常");
        }
    } else if (static_cast<quint8>(data[1]) == 0x86) {
        if (data.length() == 5) {
            if (isSetBtn) {
                isSetBtn = false;
                ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"收："+QString(data.toHex(' ').toUpper().append(' '))+"\n");
                ui->plainTextEdit_2->moveCursor(QTextCursor::End);
            }

            QByteArray dataPart = data.left(3);
            quint16 receivedCrc = (static_cast<quint8>(data[3]) << 8) | static_cast<quint8>(data[4]);
            quint16 calculatedCrc = Modbus_CRC16(reinterpret_cast<uint8_t *>(dataPart.data()), dataPart.length());
            if (receivedCrc == calculatedCrc) {
            } else {
                appendToTextEdit(Receive, "", "CRC校验不通过");
            }
            appendToTextEdit(Receive, "", "写入失败");
            if (errValueTimes < 5) {
                errValueTimes++;
            } else {
                autoScanTimer.stop();
                isAutoScan = false;         // 这里可能会有问题，有可能 自动扫描需要点两次才会扫描
                ui->pushButton_22->setStyleSheet("");
                ui->pushButton_22->setText("扫描设备");
                delay(10);
                queueClear();
                delay(10);
                errValueTimes = 0;
            }
        } else if (data.length() > 5) {
            appendToTextEdit(Receive, "", "接收数据长度异常");
        }
    }
    data.clear();
    pointerInit();
    waitingResponseTimer.stop();        // 有时候会出现06功能码只返回7个数据，所以需要报超时
    if (!functionQueue.isEmpty()) {     // 选择传感器型号时，没有用队列发，此时队列时空的，不能出队
        functionQueue.dequeue();
    }
    autoDequeueTimer.start(10);
}

// 万用表串口接收延时
void MainWindow::multimeterSerialDelay()
{
    // 关闭串口接收延时Timer
    if (MultimeterSerialDelayTimer.isActive()) {
        MultimeterSerialDelayTimer.stop();
    }

    // 开启定时器接收串口数据延时
    MultimeterSerialDelayTimer.start(50);
}

// 万用表串口接收
void MainWindow::multimeterSerialRead()
{
    // 关闭串口接收延时Timer
    if (MultimeterSerialDelayTimer.isActive()) {
        MultimeterSerialDelayTimer.stop();
    }

    // 检查串口是否开启
    if (!MultimeterSerial.isOpen()) {
        return;
    }

    // 读取所有可用数据，返回的值单位默认是V
    QByteArray data = MultimeterSerial.readAll();
    QString dataStr = QString::fromUtf8(data).trimmed();

    // dataStr是科学计数法，采用double缓冲一下
    bool ok = false;
    double valueA = dataStr.toDouble(&ok);
    int dataInt = 0;
    if (ok) {
        if (!isVoltageSensor){  // 采用变送起点label辨别当前模式是电压还是电流
            dataInt = static_cast<int>(valueA * 1000000);  // 截断为 uA，不四舍五入
            QString resultStr = QString::number(dataInt);  // 转成 QString
            appendToTextEdit(Receive, "无所谓", resultStr);
            if (LineEditCurrentCalibration1) {
                LineEditCurrentCalibration1->setText(resultStr);
                LineEditCurrentCalibration1 = nullptr;
            } else if (LineEditCurrentCalibration2) {
                LineEditCurrentCalibration2->setText(resultStr);
                LineEditCurrentCalibration2 = nullptr;
            } else if (LineEditCurrentCalibration3) {
                LineEditCurrentCalibration3->setText(resultStr);
                LineEditCurrentCalibration3 = nullptr;
            } else if (LineEditCurrentCalibration4) {
                LineEditCurrentCalibration4->setText(resultStr);
                LineEditCurrentCalibration4 = nullptr;
            } else if (LineEditCurrentCalibration5) {
                LineEditCurrentCalibration5->setText(resultStr);
                LineEditCurrentCalibration5 = nullptr;
            }
        } else {
            dataInt = static_cast<int>(valueA * 1000);  // 截断为 mV，不四舍五入
            QString resultStr = QString::number(dataInt);  // 转成 QString
            appendToTextEdit(Receive, "无所谓", resultStr);
            if (LineEditCurrentCalibration1) {
                LineEditCurrentCalibration1->setText(resultStr);
                LineEditCurrentCalibration1 = nullptr;
            } else if (LineEditCurrentCalibration2) {
                LineEditCurrentCalibration2->setText(resultStr);
                LineEditCurrentCalibration2 = nullptr;
            } else if (LineEditCurrentCalibration3) {
                LineEditCurrentCalibration3->setText(resultStr);
                LineEditCurrentCalibration3 = nullptr;
            } else if (LineEditCurrentCalibration4) {
                LineEditCurrentCalibration4->setText(resultStr);
                LineEditCurrentCalibration4 = nullptr;
            } else if (LineEditCurrentCalibration5) {
                LineEditCurrentCalibration5->setText(resultStr);
                LineEditCurrentCalibration5 = nullptr;
            }
        }
    } else {
        appendToTextEdit(Receive, "无所谓", "返回数据有误，返回数据->"+data);
    }
}

// 气压仪串口接收
void MainWindow::barographSerialRead()
{
    if (!BarographSerial.isOpen()) {
        return;
    }

    // 读取所有可用数据
    QByteArray data = BarographSerial.readAll();

    // 打印接收到的数据（16进制格式）
    qDebug() << "Received data (hex):" << data.toHex();
}

void MainWindow::interfaceInit()
{
    // 上位机标题
    this->setWindowTitle("微雀微压差变送器测试工具V3.2");
    ui->tabWidget->setCurrentIndex(0);

    // 超时次数为0
    timeoutTimes    = 0;
    errValueTimes   = 0;

    // 设备ID初始化为FF
    ui->lineEdit_7->setText("1");
    receDeviceID = "1";
    decimalPoint = 1;       // 压力显示小数点位初始为1 kP时为4

    isDeviceID = false;
    setpressureUnit = false;

    // 0Pa校正不显示
    ui->pushButton_35->hide();
    ui->pushButton_36->hide();
    ui->lineEdit_16->hide();
    ui->pushButton_23->hide();

    // 电流标定的占空比显示
    ui->lineEdit_27->setText("17");
    ui->lineEdit_28->setText("30");
    ui->lineEdit_29->setText("50");
    ui->lineEdit_30->setText("75");
    ui->lineEdit_31->setText("85");

    // 默认为电流传感器
    isVoltageSensor = false;

    // 变送起点
    ui->lineEdit_23->setText("4000");
    ui->label_25->setText("uA");

    // 变送满度
    ui->lineEdit_25->setText("20000");
    ui->label_28->setText("uA");

    // 电流限制
    ui->lineEdit_9->setText("3800");
    ui->lineEdit_10->setText("20200");
    ui->groupBox_6->setTitle("电流标定");

    // 传感器按钮初始化
    buttonList = {
        ui->pushButton_27, ui->pushButton_30, ui->pushButton_31, ui->pushButton_51, ui->pushButton_52, ui->pushButton_102,
        ui->pushButton_53, ui->pushButton_106, ui->pushButton_105, ui->pushButton_104, ui->pushButton_103
    };

    // 电流标定按钮初始化, 未加读取参数
    currCaliButtonList = {
        ui->pushButton_38, ui->pushButton_39, ui->pushButton_40, ui->pushButton_41, ui->pushButton_42, ui->pushButton_48,
        ui->pushButton_46, ui->pushButton_45, ui->pushButton_44, ui->pushButton_47, ui->pushButton_43, ui->pushButton_50,
        ui->pushButton_25,
    };

    // 传感器Map初始化
    sensorListMap = {
        //LW
        {"1K", "15011"},        //1K   LWLP5001DD
        {"2K", "15021"},        //2K   LWLP5002DD
        {"5K", "15061"},        //5K   LWLP5006DD
        {"10K", "15101"},       //10K  LWLP5010DD
        {"20K", "15201"},       //20K  LWLP5020DD
        {"40K", "15401"},       //40K  LWLP5040DD
        {"100K", "15111"},      //100k LWLP5100DD
        // TE
        {"TE_1K", "54012"},     //1K  SM9541_010C_DC3S
        {"TE_2K", "54022"},     //2K  SM9541_020C_DC3S
        {"TE_4K", "54042"},     //4K  SM9541_040C_DC3S
        {"TE_10K", "54102"},    //10K SM9541_100C_DC3S
    };

    // 电压传感器初始化
    voltageSensorListMap = {
        //LW
        {"1K", "4097"},        //1K   LWLP5001DD   0x1001
        {"2K", "4098"},        //2K   LWLP5002DD   0x1002
        {"5K", "4102"},        //5K   LWLP5006DD   0x1006
        {"10K", "4106"},       //10K  LWLP5010DD   0x100A
        {"20K", "4116"},       //20K  LWLP5020DD   0x1014
        {"40K", "4136"},       //40K  LWLP5040DD   0x1028
        {"100K", "4196"},      //100k LWLP5100DD   0x1064
        // TE
        {"TE_1K", "8449"},     //1K  SM9541_010C_DC3S  0x2101
        {"TE_2K", "8450"},     //2K  SM9541_020C_DC3S  0x2102
        {"TE_4K", "8452"},     //4K  SM9541_040C_DC3S  0x2104
        {"TE_10K", "8458"},    //10K SM9541_100C_DC3S  0x210A
    };

    //变送方式初始化
    transmissionMethodListMap = {
        {"4mA~20mA", "0"},
        {"0V~5V" , "1"},
        {"0V~10V", "2"},
    };

    // 校验位列表初始化
    ui->comboBox_4->addItem("无校验");
    ui->comboBox_4->addItem("奇校验");
    ui->comboBox_4->addItem("偶校验");

    // 波特率列表初始化
    baudMap = {
        {"9600",   "0"},
        {"2400",   "1"},
        {"4800",   "2"},
        {"14400",  "3"},
        {"19200",  "4"},
        {"38400",  "5"},
        {"56000",  "6"},
        {"57600",  "7"},
        {"115200", "8"},
    };

    voltageBaudMap = {
        {"1200",   "0"},
        {"2400",   "1"},
        {"4800",   "2"},
        {"9600",   "3"},
        {"19200",  "4"},
        {"38400",  "5"},
        {"57600",  "6"},
        {"115200", "7"},
    };

    // 波特率列表
    for (auto it = baudMap.constBegin(); it != baudMap.constEnd(); ++it) {
        ui->comboBox_6->addItem(it.key());  // 显示型号，绑定ID
    }

    // 1010把电流上下限的读写按钮隐藏,使用hide方法会导致控件错位问题，把高度设置为0可以实现按钮隐藏
    // ui->pushButton_16->setFixedHeight(0);
    // ui->pushButton_17->setFixedHeight(0);
    // ui->pushButton_18->setFixedHeight(0);
    // ui->pushButton_19->setFixedHeight(0);

    // ui->pushButton_38->setDisabled(true);      // 电流标定失能
    // ui->pushButton_39->setDisabled(true);
    // ui->pushButton_40->setDisabled(true);
    // ui->pushButton_41->setDisabled(true);
    // ui->pushButton_42->setDisabled(true);
    // ui->pushButton_43->setDisabled(true);
    // ui->pushButton_44->setDisabled(true);
    // ui->pushButton_45->setDisabled(true);
    // ui->pushButton_46->setDisabled(true);
    // ui->pushButton_47->setDisabled(true);
    // ui->pushButton_48->setDisabled(true);
    // ui->pushButton_50->setDisabled(true);

    // ui->pushButton_69->setDisabled(true);      // 传感器标定失能
    // ui->pushButton_70->setDisabled(true);
    // ui->pushButton_71->setDisabled(true);
    // ui->pushButton_72->setDisabled(true);
    // ui->pushButton_73->setDisabled(true);
    // ui->pushButton_74->setDisabled(true);
    // ui->pushButton_76->setDisabled(true);

    // ui->pushButton_25->setDisabled(true);       // 一键标定失能
}

void MainWindow::regInit()
{
    QList<RegisterInfo> registerList = {
        {"FW_VER", "40000", "1","uint16","0","固件版本"},
        {"TIMESTAMP" ,"40001", "2","uint32","0","固件信息"},
        {"REG_1I_TEMPATURE", "40003", "1", "uint16", "0", "当前温度，个位是小数位。比如寄存器的值为256，实际温度为25.6℃"},
        {"REG_2F_PRESSURE_Pa", "40004", "2", "float", "0", "当前压力，单位pa"},
        {"REG_2F_PRESSURE_UNIT", "40006", "2", "float", "0", "当前压力，单位由压力单位寄存器决定"},
        {"REG_1I_SEN_INNERCODE", "40009", "1", "uint16", "0", "传感器内码"},
        {"REG_1I_I_OUTPUT_uA", "40008", "1", "uint16", "0", "当前电流,单位uA"},
        {"REG_1U_I_PWM", "40010", "1", "uint16", "0", "电流输出对应的PWM"},
        {"REG_1U_I_STATE", "40011", "1", "uint16", "0", "状态寄存器"},
        {"REG_1U_CALIB_DATA_CMD", "40012", "1", "uint16", "0", "0x30:置零；0xAD53:保存电流标定参数；0x8D73:保存传感器标定参数；0x9D63:清除传感器标定参数；0xCD43:清除电流标定参数；0x4253:退出其他模式，进入变送模式"},
        {"REG_1U_SYS_CMD", "40014", "1", "uint16", "0", "系统命令：0x52AD 重启，0x46B9 清除某模式"},
        {"REG_1U_TRANS_CMD" ,"40015",  "1","uint16","0","变送命令"},
        {"DEVICE_ID", "40016", "1", "uint16", "0", "设备站号"},
        {"REG_1U_COM_BAUDE", "40017", "1", "uint16", "0", "波特率"},
        {"REG_1U_SEN_UNIT", "40018", "1", "uint16", "0", "压力单位"},
        {"REG_1U_TRANS_MODE", "40019", "1", "uint16", "0", "变送模式，0,4~20mA    1,0V~5V     2,0V~10V       3,0~20mA"},
        {"REG_2F_TRANS_STAR_Pa", "40023", "2", "float", "0", "变送器变送零点气压，单位pa"},
        {"REG_2F_TRANS_FSC_Pa", "40025", "2", "float", "0", "变送器变送满度气压，单位pa"},
        {"REG_1U_ZERO_RANGE_01Pa", "40027", "1", "uint16", "0", "零点归零范围设置，在此范围内，变送输出电流均为零点电流(单位0.1Pa）"},
        {"REG_1U_VI_OUTPUT_MIN_uA", "40021", "1", "uint16", "0", "电流输出最小值"},
        {"REG_1U_VI_OUTPUT_MAX_uA", "40022", "1", "uint16", "0", "电流输出最大值"},
        {"REG_1U_VI_Trans_Start_uAmV", "40030", "1", "uint16", "0", "变送输出起点信号"},
        {"REG_1U_VI_Trans_FSC_uAmV", "40032", "1", "uint16", "0", "变送输出满度信号"},
        {"REG_1U_BACK_LIGHT_SET", "40033", "1", "uint16", "0", "背光工作模式设置"},
        {"REG_1U_SensorType", "40097", "1", "uint16", "0", "传感器型号"},
        {"REG_2I_SensorMin_code", "40098", "2", "int32", "0", "数字传感器最小内码值"},
        {"REG_2I_SensorMax_code", "40100", "2", "int32", "0", "数字传感器满度内码值"},
        {"REG_2F_SensorMinVal_cmH2O", "40102", "2", "float", "0", "传感器最小输出值，零点值  (单位:cmH2O)"},
        {"REG_2F_SensorMaxVal_cmH2O", "40104", "2", "float", "0", "传感器最大输出值，满量程值 (单位:cmH2O)"},
        {"REG_2F_SensorMinVal_Pa", "40106", "2", "float", "0", "传感器最小输出值，零点值  (单位:Pa)"},
        {"REG_2F_SensorMaxVal_Pa", "40108", "2", "float", "0", "传感器最大输出值，零点值  (单位:Pa)"},
        {"REG_1U_I_Calib_flg", "40064", "1", "uint16", "0", "电流校准标志， I_CALIBRATE_FLG"},
        {"REG_1U_I_CalibPointNum", "40065", "1", "uint16", "0", "电流标定点数"},
        {"REG_1U_PWM", "40040", "1", "uint16", "0", "设置PWM占空比,输入参数0~100"},
        {"REG_1U_I_Calib1_CurVal", "40041", "1", "uint16", "0", "电流输出校准点1电流"},
        {"REG_1U_I_Calib2_CurVal", "40042", "1", "uint16", "0", "电流输出校准点2电流"},
        {"REG_1U_I_Calib3_CurVal", "40043", "1", "uint16", "0", "电流输出校准点3电流"},
        {"REG_1U_I_Calib4_CurVal", "40044", "1", "uint16", "0", "电流输出校准点4电流"},
        {"REG_1U_I_Calib5_CurVal", "40045", "1", "uint16", "0", "电流输出校准点5电流"},
        {"REG_2U_I_Calib1_PWM_Val", "40066", "2", "uint32", "0", "电流输出校准点1对应的PWM定时器计数值"},
        {"REG_2U_I_Calib2_PWM_Val", "40068", "2", "uint32", "0", "电流输出校准点2对应的PWM定时器计数值"},
        {"REG_2U_I_Calib3_PWM_Val", "40070", "2", "uint32", "0", "电流输出校准点3对应的PWM定时器计数值"},
        {"REG_2U_I_Calib4_PWM_Val", "40072", "2", "uint32", "0", "电流输出校准点4对应的PWM定时器计数值"},
        {"REG_2U_I_Calib5_PWM_Val", "40074", "2", "uint32", "0", "电流输出校准点5对应的PWM定时器计数值"},
        {"REG_1U_sensorCalib_flg", "40079", "1", "uint16", "0", ""},
        {"REG_1U_sensorCalibPointNum", "40080", "1", "uint16", "0", "传感器标定点数量"},
        {"REG_2F_sensor_Calib1_Pa", "40050", "2", "float", "0", "传感器标定1"},
        {"REG_2F_sensor_Calib2_Pa", "40052", "2", "float", "0", "传感器标定2"},
        {"REG_2F_sensor_Calib3_Pa", "40054", "2", "float", "0", "传感器标定3"},
        {"REG_2F_sensor_Calib4_Pa", "40056", "2", "float", "0", "传感器标定4"},
        {"REG_2F_sensor_Calib5_Pa", "40058", "2", "float", "0", "传感器标定5"},
        {"REG_2I_sensor_0Pa_innercode", "40081", "2", "int32", "0", "传感器变送两点偏移量"},
        {"REG_2I_sensor_1Pa_innercode", "40083", "2", "int32", "0", ""},
        {"REG_2I_sensor_2Pa_innercode", "40085", "2", "int32", "0", ""},
        {"REG_2I_sensor_3Pa_innercode", "40087", "2", "int32", "0", ""},
        {"REG_2I_sensor_4Pa_innercode", "40089", "2", "int32", "0", ""},
        {"REG_2I_sensor_5Pa_innercode", "40091", "2", "int32", "0", ""},
        {"REG_2I_TransZero_InnerCode", "40093", "1", "uint16", "0", ""},
        {"REG_2I_TransFSC_InnerCode", "40095", "1", "uint16", "0", ""},
        {"REG_1U_init_flg1", "40200", "1", "uint16", "0", "初始化标志"},
        {"REG_1U_init_flg2", "40201", "1", "uint16", "0", "数据结构校验和"},
        // 0906新增
        {"REG_1U_VI_OUTPUT_MAX_mV", "40034", "1", "uint16", "0", "电压输出最大值"},
        {"REG_2F_PRESSURE_Pa", "40035", "2", "float", "0", "压力测量值"},
        // 李工分辨率较低，加几个预留
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
        {" ", " ", " ", " ", " ", " "},
    };

    // 清空第三页原有内容（如果有的话）
    QWidget* thirdPage = ui->tabWidget->widget(2); // 获取第三页的widget
    QLayout* existingLayout = thirdPage->layout();
    if (existingLayout) {
        QLayoutItem* item;
        while ((item = existingLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete existingLayout;
    }

    // 创建表格并添加到第三页
    QTableWidget* table = new QTableWidget(registerList.size(), 6); // 行数=寄存器数量，列数=6
    table->setHorizontalHeaderLabels({"名称", "地址", "长度", "类型", "其他", "备注"});

    // 填充数据
    for (int i = 0; i < registerList.size(); ++i) {
        const RegisterInfo& info = registerList[i];
        table->setItem(i, 0, new QTableWidgetItem(info.name));
        table->setItem(i, 1, new QTableWidgetItem(info.address));
        table->setItem(i, 2, new QTableWidgetItem(info.count));
        table->setItem(i, 3, new QTableWidgetItem(info.type));
        table->setItem(i, 4, new QTableWidgetItem(info.value));
        table->setItem(i, 5, new QTableWidgetItem(info.description));
    }

    // 设置自适应属性
    table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents); // 关键设置
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    // 设置布局
    QVBoxLayout* layout = new QVBoxLayout(thirdPage);
    layout->addWidget(table);
    thirdPage->setLayout(layout);
}

void MainWindow::pushButtonInit()
{
    // 传感器按钮列表
    for (QPushButton *btn : buttonList) {
        pushButtonDisEnable(btn);
    };

    // 电流标定按钮列表
    for (QPushButton *btn : currCaliButtonList) {
        pushButtonDisEnable(btn);
    };

    // 参数配置按钮
    pushButtonDisEnable(ui->pushButton_13);
    pushButtonDisEnable(ui->pushButton_15);
    pushButtonDisEnable(ui->pushButton_17);
    pushButtonDisEnable(ui->pushButton_19);
}

// 延时函数
void MainWindow::delay(int time)
{    
    QEventLoop loop;
    QTimer::singleShot(time, &loop, &QEventLoop::quit);
    loop.exec();
}

// 自动扫描函数
void MainWindow::autoScan()
{
    // 如果自动扫描按钮没有按下，不入队
    if (!isAutoScan) {
        autoScanTimer.stop();
        return;
    }

    // 如果队列不是空的，返回
    if (!functionQueue.isEmpty()) return;

    if (!isVoltageSensor) {
        // 当前压力
        functionQueue.enqueue([this]() {
            isFloat = true;
            LineEditPressure = ui->lineEdit;
            serialRead(Portsensor, "0004", "02");
        });

        // 当前内码
        functionQueue.enqueue([this]() {
            isFloat = false;
            LineEditInternalCode = ui->lineEdit_5;
            serialRead(Portsensor, "0009", "01");
        });

        // 当前电流
        functionQueue.enqueue([this]() {
            LineEditCurrent = ui->lineEdit_2;
            serialRead(Portsensor, "0008", "01");
        });

        // 当前PWM
        functionQueue.enqueue([this]() {
            LineEditPWM = ui->lineEdit_3;
            serialRead(Portsensor, "000A", "01");
        });

        // 当前温度
        functionQueue.enqueue([this]() {
            isTemperature = true;
            serialRead(Portsensor, "0003", "01");
        });

        // 当前压力单位
        functionQueue.enqueue([this]() {
            pressureUnit = true;
            serialRead(Portsensor, "0012", "01");
        });
    } else {
        functionQueue.enqueue([this]() {
            isFloat = true;
            LineEditPressure = ui->lineEdit;
            serialRead(Portsensor, "0023", "02");
        });

        // 温度
        ui->lineEdit_4->setText("####");

        // 当前内码
        functionQueue.enqueue([this]() {
            isFloat = false;
            LineEditInternalCode = ui->lineEdit_5;
            serialRead(Portsensor, "0009", "01");
        });

        // 当前电流
        functionQueue.enqueue([this]() {
            LineEditCurrent = ui->lineEdit_2;
            serialRead(Portsensor, "0008", "01");
        });

        // 当前PWM
        functionQueue.enqueue([this]() {
            LineEditPWM = ui->lineEdit_3;
            serialRead(Portsensor, "000A", "01");
        });

        // 当前压力单位
        functionQueue.enqueue([this]() {
            pressureUnit = true;
            serialRead(Portsensor, "0002", "01");
        });
    }

    ui->pushButton_22->setStyleSheet("background-color: #00BFFF;");
    ui->pushButton_22->setText("关闭扫描");
}

// 关闭事件
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 关闭串口
    if (sensorSerial.isOpen()) {
        sensorSerial.close();
    }
    // 清空队列
    queueClear();
    // 关闭自动扫描时间
    if (autoDequeueTimer.isActive()) {
        autoDequeueTimer.stop();
    }
    // 串口接收超时时间
    if (waitingResponseTimer.isActive()) {
        waitingResponseTimer.stop();
    }

    event->accept();  // 接受关闭事件
}

// 传感器刷新串口
void MainWindow::on_pushButton_clicked()
{
    // 清除所有串口
    ui->comboBox->clear();

    // 遍历电脑串口
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        ui->comboBox->addItem(info.portName()); // portName 存储在 UserRole
    }
}

// 万用表刷新串口
void MainWindow::on_pushButton_3_clicked()
{
    // 清除所有串口
    ui->comboBox_2->clear();

    // 遍历电脑串口
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        ui->comboBox_2->addItem(info.portName()); // portName 存储在 UserRole
    }
}

// 气压仪刷新串口
void MainWindow::on_pushButton_5_clicked()
{
    // 清除所有串口
    ui->comboBox_3->clear();

    // 遍历电脑串口
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        ui->comboBox_3->addItem(info.portName()); // portName 存储在 UserRole
    }
}

// 传感器打开
void MainWindow::on_pushButton_2_clicked()
{
    if (!sensorSerialOpen) {
        QString portName = ui->comboBox->currentText();
        sensorSerial.setPortName(portName);

        if (!sensorSerial.open(QIODevice::ReadWrite)) {
            QMessageBox::critical(this, "错误", QString("串口 %1 已被占用或无法打开").arg(portName));
            return;
        }

        // 创建波特率选择对话框
        QDialog dialog(this);
        dialog.setWindowTitle("串口设置");
        dialog.setModal(true);
        dialog.setFixedSize(200, 150); // 固定对话框大小

        // 创建布局和控件
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QComboBox *baudRateCombo = new QComboBox();
        QComboBox *parityCombo = new QComboBox();
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);

        baudRateCombo->setFixedSize(85, 35);
        QFont font = baudRateCombo->font();
        font.setPointSize(13);  // 设置字体大小为13
        baudRateCombo->setFont(font);

        parityCombo->setFixedSize(85, 35);
        parityCombo->setFont(font);

        // 添加常用的波特率选项
        baudRateCombo->addItem("1200", 1200);
        baudRateCombo->addItem("2400", 2400);
        baudRateCombo->addItem("4800", 4800);
        baudRateCombo->addItem("9600", 9600);
        baudRateCombo->addItem("57600", 57600);
        baudRateCombo->addItem("14400", 14400);
        baudRateCombo->addItem("19200", 19200);
        baudRateCombo->addItem("38400", 38400);
        baudRateCombo->addItem("56000", 56000);
        baudRateCombo->addItem("115200", 115200);
        baudRateCombo->setCurrentIndex(3); // 默认选择9600

        // 添加校验位选项
        parityCombo->addItem("无校验", QSerialPort::NoParity);
        parityCombo->addItem("奇校验", QSerialPort::OddParity);
        parityCombo->addItem("偶校验", QSerialPort::EvenParity);
        parityCombo->setCurrentIndex(0); // 默认选择无校验

        // 添加到布局
        layout->addWidget(new QLabel("请选择波特率和校验位:"));
        layout->addWidget(baudRateCombo);
        layout->addWidget(parityCombo);
        layout->addWidget(buttonBox);

        // 连接按钮信号
        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

        // 显示对话框并等待用户选择
        if (dialog.exec() == QDialog::Accepted) {
            int selectedBaudRate = baudRateCombo->currentData().toInt();
            QSerialPort::Parity selectedParity = static_cast<QSerialPort::Parity>(parityCombo->currentData().toInt());
            sensorSerial.setBaudRate(selectedBaudRate);
            sensorSerial.setDataBits(QSerialPort::Data8);
            sensorSerial.setParity(selectedParity);
            sensorSerial.setStopBits(QSerialPort::OneStop);
            sensorSerial.setFlowControl(QSerialPort::NoFlowControl);
        }
        sensorSerialOpen = true;
        ui->pushButton_2->setText("关闭");
        ui->pushButton_2->setStyleSheet("background-color: #00BFFF;");
        ui->comboBox->setEnabled(false);
        ui->pushButton->setEnabled(false);

        // 清空队列
        delay(10);
        queueClear();
        delay(10);
        if (waitingResponseTimer.isActive()) {
            waitingResponseTimer.stop();
        }      
        // 开启自动出队定时器
        autoDequeueTimer.start(10);
    } else {
        sensorSerial.close();
        sensorSerialOpen = false;
        ui->pushButton_2->setText("打开");
        ui->pushButton_2->setStyleSheet("");
        ui->pushButton_22->setStyleSheet("");
        ui->pushButton_22->setText("扫描设备");
        ui->pushButton_26->setStyleSheet("");
        ui->pushButton_28->setStyleSheet("");
        ui->pushButton_29->setStyleSheet("");
        ui->pushButton_108->setStyleSheet("");
        ui->pushButton_109->setStyleSheet("");
        for (QPushButton *btn : buttonList) {
            pushButtonDisEnable(btn);
        };
        ui->comboBox->setEnabled(true);
        ui->pushButton->setEnabled(true);

        delay(10);
        queueClear();
        delay(10);
        autoDequeueTimer.stop();
        autoScanTimer.stop();
    }
}

// 万用表打开
void MainWindow::on_pushButton_4_clicked()
{
    if (!MultimeterSerialOpen) {
        // 设置你要连接的串口名，例如从 ComboBox 获取
        QString portName = ui->comboBox_2->currentText();  // 假设你用了 comboBox 选择串口
        MultimeterSerial.setPortName(portName);

        // 先尝试 open 看是否已被占用
        if (!MultimeterSerial.open(QIODevice::ReadWrite)) {
            QMessageBox::critical(this, "错误", QString("串口 %1 已被占用或无法打开").arg(portName));
            return;
        }

        // 设置参数（如果 open 成功）
        MultimeterSerial.setBaudRate(QSerialPort::Baud9600);
        MultimeterSerial.setDataBits(QSerialPort::Data8);
        MultimeterSerial.setParity(QSerialPort::NoParity);
        MultimeterSerial.setStopBits(QSerialPort::OneStop);

        MultimeterSerialOpen = true;
        ui->pushButton_4->setText("关闭");
        ui->pushButton_4->setStyleSheet("background-color: #00BFFF;");
        ui->comboBox_2->setEnabled(false);
        ui->pushButton_3->setEnabled(false);

        // 打开串口先发送一条命令
    }
    else {
        MultimeterSerial.close();
        MultimeterSerialOpen = false;
        ui->pushButton_4->setText("打开");
        ui->pushButton_4->setStyleSheet("");
        ui->comboBox_2->setEnabled(true);
        ui->pushButton_3->setEnabled(true);
    }
}

// 气压仪打开
void MainWindow::on_pushButton_6_clicked()
{
    if (!BarographSerialOpen) {
        // 设置你要连接的串口名，例如从 ComboBox 获取
        QString portName = ui->comboBox_3->currentText();  // 假设你用了 comboBox 选择串口
        BarographSerial.setPortName(portName);

        // 先尝试 open 看是否已被占用
        if (!BarographSerial.open(QIODevice::ReadWrite)) {
            QMessageBox::critical(this, "错误", QString("串口 %1 已被占用或无法打开").arg(portName));
            return;
        }

        // 设置参数（如果 open 成功）
        BarographSerial.setBaudRate(QSerialPort::Baud9600);
        BarographSerial.setDataBits(QSerialPort::Data8);
        BarographSerial.setParity(QSerialPort::NoParity);
        BarographSerial.setStopBits(QSerialPort::OneStop);

        BarographSerialOpen = true;
        ui->pushButton_6->setText("关闭");
        ui->pushButton_6->setStyleSheet("background-color: #00BFFF;");
        ui->comboBox_3->setEnabled(false);
        ui->pushButton_5->setEnabled(false);
    }
    else {
        BarographSerial.close();
        BarographSerialOpen = false;
        ui->pushButton_6->setText("打开");
        ui->pushButton_6->setStyleSheet("");
        ui->comboBox_3->setEnabled(true);
        ui->pushButton_5->setEnabled(true);
    }
}

// 清除对话框内容
void MainWindow::on_pushButton_7_clicked()
{
    // 按下变绿
    ui->pushButton_7->setStyleSheet(R"(
            QPushButton:pressed {
                background-color:rgba(0,255,0);
            }
        )");
    ui->plainTextEdit->clear();
    ui->plainTextEdit->appendPlainText("         监测数据");
}

// 恢复出厂设置
void MainWindow::on_pushButton_35_clicked()
{
    if (!(QMessageBox::information(this, "警告", "是否要重启设备", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)) return;

    if (!isVoltageSensor) {
        functionQueue.prepend([this]() {
            appendToTextEdit(Write, "恢复出厂设置命令", "恢复出厂设置");
            serialWrite(Portsensor, "000E", "C381");
        });
    } else {
        functionQueue.prepend([this]() {
            appendToTextEdit(Write, "恢复出厂设置命令", "恢复出厂设置");
            serialWrite(Portsensor, "0010", "01");
        });
    }
}

// 读设备地址
void MainWindow::on_pushButton_12_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            ui->lineEdit_7->setText("1");
            LineEditID = ui->lineEdit_7;
            appendToTextEdit(Read, "设备地址", "");
            serialRead(Portsensor, "0010", "01");
        });
    } else {
        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            appendToTextEdit(Read, "设备地址", "");
            serialRead(Portsensor, "0000", "01");
        });
    }
}

// 读波特率
void MainWindow::on_pushButton_14_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.prepend([this]() {
            ComboBoxBaud = ui->comboBox_6;
            appendToTextEdit(Read, "波特率", "");
            serialRead(Portsensor, "0011", "01");
        });
    } else {
        functionQueue.prepend([this]() {
            ComboBoxBaud = ui->comboBox_6;
            appendToTextEdit(Read, "波特率", "");
            serialRead(Portsensor, "0001", "01");
        });
    }
}

// 读电流下限
void MainWindow::on_pushButton_16_clicked()
{

    functionQueue.enqueue([this]() {
        ui->lineEdit_9->setText("");
        LineEditCurrentMin = ui->lineEdit_9;
        appendToTextEdit(Read, "电流/电压下限", "");
        serialRead(Portsensor, "0015", "01");
    });
}

// 读电流上限
void MainWindow::on_pushButton_18_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            ui->lineEdit_10->setText("");
            LineEditCurrentMax = ui->lineEdit_10;
            appendToTextEdit(Read, "电流/电压上限", "");
            serialRead(Portsensor, "0016", "01");
        });
    } else {
        functionQueue.enqueue([this]() {
            ui->lineEdit_10->setText("");
            LineEditCurrentMax = ui->lineEdit_10;
            appendToTextEdit(Read, "电流/电压上限", "");
            serialRead(Portsensor, "0022", "01");
        });
    }
}

// 读零点跟踪
void MainWindow::on_pushButton_20_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            ui->lineEdit_11->setText("");
            LineEditZeroTracking = ui->lineEdit_11;
            appendToTextEdit(Read, "零点跟踪", "");
            serialRead(Portsensor, "001B", "01");
        });
    } else {
        functionQueue.enqueue([this]() {
            ui->lineEdit_11->setText("");
            LineEditZeroTracking = ui->lineEdit_11;
            appendToTextEdit(Read, "零点跟踪", "");
            serialRead(Portsensor, "000C", "01");
        });
    }
}

// 重启设备
void MainWindow::on_pushButton_36_clicked()
{
    // 防止误操作
   if (QMessageBox::information(this, "警告", "是否要重启设备", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // 插队进入
        functionQueue.prepend([this]() {
            appendToTextEdit(Write, "重启设备命令", "重启");
            serialWrite(Portsensor, "000E", "E082");
        });
    } else {
        return;
    }
}

// 一键读取
void MainWindow::on_pushButton_37_clicked()
{
    // 传感器按钮初始化
    for (QPushButton *btn : buttonList) {
        pushButtonDisEnable(btn);
    };

    // 自动出队会异常关闭，在这里打开一下
    if (autoDequeueTimer.isActive()) {

    } else {
        pointerInit();
        waitingResponseTimer.stop();        // 有时候会出现06功能码只返回7个数据，所以需要报超时
        if (!functionQueue.isEmpty()) {     // 选择传感器型号时，没有用队列发，此时队列时空的，不能出队
            functionQueue.dequeue();
        }
        autoDequeueTimer.start(10);
    };

    autoScanTimer.stop();

    delay(10);
    // 清除队列
    queueClear();
    delay(10);

    ui->pushButton_37->setEnabled(false);

    if (!isVoltageSensor) {
        // 读取固件版本
        functionQueue.enqueue([this]() {
            isFirmwareVersion = true;
            appendToTextEdit(Read, "读固件版本", "");
            serialRead(Portsensor, "0000", "01");
        });

        // 读固件信息
        functionQueue.enqueue([this]() {
            isFirmwareinfo = true;
            appendToTextEdit(Read, "读固件信息", "");
            serialRead(Portsensor, "0001", "02");
        });

        // 当前压力
        functionQueue.enqueue([this]() {
            isFloat = true;
            ui->lineEdit->setText("");
            LineEditPressure = ui->lineEdit;
            serialRead(Portsensor, "0004", "02");
        });

        // 读设备地址
        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            appendToTextEdit(Read, "设备地址", "");
            serialRead(Portsensor, "0010", "01");
        });

        // 读波特率
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            appendToTextEdit(Read, "波特率", "");
            serialRead(Portsensor, "0011", "01");
        });

        // 当前温度
        functionQueue.enqueue([this]() {
            ui->lineEdit_4->setText("");
            isTemperature = true;
            appendToTextEdit(Read, "温度", "");
            serialRead(Portsensor, "0003", "01");
        });

        // 读零点跟踪
        functionQueue.enqueue([this]() {
            ui->lineEdit_11->setText("");
            LineEditZeroTracking = ui->lineEdit_11;
            serialRead(Portsensor, "001B", "01");
            appendToTextEdit(Read, "零点跟踪", "");
        });

        // 读电流上限
        functionQueue.enqueue([this]() {
            ui->lineEdit_10->setText("");
            LineEditCurrentMax = ui->lineEdit_10;
            serialRead(Portsensor, "0016", "01");
            appendToTextEdit(Read, "电流/电压上限", "");
        });

        // 当前压力单位
        functionQueue.enqueue([this]() {
            pressureUnit = true;
            serialRead(Portsensor, "0012", "01");
        });
    } else {
        // 读设备地址
        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            appendToTextEdit(Read, "设备地址", "");
            serialRead(Portsensor, "0000", "01");
        });

        // 读波特率
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0001", "01");
            appendToTextEdit(Read, "波特率", "");
        });

        // 读小数点位
        functionQueue.enqueue([this]() {
            ui->lineEdit_16->setText("");
            LineEditPoint = ui->lineEdit_16;
            serialRead(Portsensor, "0003", "01");
            appendToTextEdit(Read, "小数点位", "");
        });

        // 温度
        ui->lineEdit_4->setText("####");

        functionQueue.enqueue([this]() {
            isFloat = true;
            ui->lineEdit->setText("");
            LineEditPressure = ui->lineEdit;
            serialRead(Portsensor, "0023", "02");
        });

        // 串口校验位
        functionQueue.enqueue([this]() {
            ui->comboBox_4->currentIndex();
            ComboBoxCheck = ui->comboBox_4;
            serialRead(Portsensor, "0025", "01");
            appendToTextEdit(Read, "当前校验位", "");
        });

        // 读零点跟踪
        functionQueue.enqueue([this]() {
            ui->lineEdit_11->setText("");
            LineEditZeroTracking = ui->lineEdit_11;
            serialRead(Portsensor, "000C", "01");
            appendToTextEdit(Read, "零点跟踪", "");
        });

        // 读电流上限
        functionQueue.enqueue([this]() {
            ui->lineEdit_10->setText("");
            LineEditCurrentMax = ui->lineEdit_10;
            serialRead(Portsensor, "0022", "01");
            appendToTextEdit(Read, "电流/电压上限", "");
        });

        // 当前压力单位
        functionQueue.enqueue([this]() {
            pressureUnit = true;
            serialRead(Portsensor, "0002", "01");
        });
    }

    // 读取传感器变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_24->setText("");
        LineEditStartPointPa = ui->lineEdit_24;
        serialRead(Portsensor, "0017", "02");
        appendToTextEdit(Read, "传感器变送起点（Pa）", "");
    });

    // 读取传感器变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_26->setText("");
        LineEditEndPointPa = ui->lineEdit_26;
        serialRead(Portsensor, "0019", "02");
        appendToTextEdit(Read, "传感器变送满度（Pa）", "");
    });

    // 读变送方式，
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0013", "01");
        isTransmissionMethod = true;
        appendToTextEdit(Read, "读变送方式", "");
    });

    // 当前内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_5->setText("");
        LineEditInternalCode = ui->lineEdit_5;
        serialRead(Portsensor, "0009", "01");
    });

    // 当前电流
    functionQueue.enqueue([this]() {
        ui->lineEdit_2->setText("");
        LineEditCurrent = ui->lineEdit_2;
        serialRead(Portsensor, "0008", "01");
    });

    // 当前PWM
    functionQueue.enqueue([this]() {
        ui->lineEdit_3->setText("");
        LineEditPWM = ui->lineEdit_3;
        serialRead(Portsensor, "000A", "01");
    });

    // 读电流下限
    functionQueue.enqueue([this]() {
        ui->lineEdit_9->setText("");
        LineEditID = ui->lineEdit_9;
        serialRead(Portsensor, "0015", "01");
        appendToTextEdit(Read, "电流/电压下限", "");
    });

    // 插队读传感器型号
    functionQueue.enqueue([this]() {
        isSensor = true;
        serialRead(Portsensor, "0061", "01");
        appendToTextEdit(Read, "传感器型号", "");
    });

    // 插队读传感器最小输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_17->setText("");
        LineEditOutMinPa = ui->lineEdit_17;
        serialRead(Portsensor, "006A", "02");
        appendToTextEdit(Read, "传感器输出最小值（pa）", "");
    });

    // 插队读传感器最大输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_18->setText("");
        LineEditOutMaxPa = ui->lineEdit_18;
        serialRead(Portsensor, "006C", "02");
        appendToTextEdit(Read, "传感器输出最大值（pa）", "");
    });

    // 插队读传感器最小输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_19->setText("");
        LineEditOutMincmH2O = ui->lineEdit_19;
        serialRead(Portsensor, "0066", "02");
        appendToTextEdit(Read, "传感器输出最小值（cmH2O）", "");
    });

    // 插队读传感器最大输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_20->setText("");
        LineEditOutMaxcmH2O = ui->lineEdit_20;
        serialRead(Portsensor, "0068", "02");
        appendToTextEdit(Read, "传感器输出最大值（cmH2O）", "");
    });

    // 插队读传感器最小内码值
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_21->setText("");
        LineEditMinInternalCode = ui->lineEdit_21;
        serialRead(Portsensor, "0062", "02");
        appendToTextEdit(Read, "传感器输出最小内码值", "");
    });

    // 插队读传感器满度内码值
    functionQueue.enqueue([this]() {
        ui->lineEdit_22->setText("");
        LineEditMaxInternalCode = ui->lineEdit_22;
        serialRead(Portsensor, "0064", "02");
        appendToTextEdit(Read, "传感器输出满度内码值", "");
    });

    // 插队读取传感器变送起点uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_23->setText("");
        LineEditStartPointuA = ui->lineEdit_23;
        serialRead(Portsensor, "001E", "01");
        appendToTextEdit(Read, "传感器变送起点（uA/mV）", "");
    });

    // 插队读取传感器变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_25->setText("");
        LineEditEndPointuA = ui->lineEdit_25;
        serialRead(Portsensor, "0020", "01");
        appendToTextEdit(Read, "传感器变送满度（uA/mV）", "");
    });

    // 读取电流标定点数
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_13->setText("");
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 读取电流标定点1电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_37->setText("");
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        serialRead(Portsensor, "0029", "01");
        appendToTextEdit(Read, "电流标定点1", "");
    });

    // 读取电流标定点1对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_32->setText("");
        LineEditInternalCode1 = ui->lineEdit_32;
        serialRead(Portsensor, "0042", "02");
        appendToTextEdit(Read, "电流标定内码1", "");
    });

    // 读取电流标定点2电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_41->setText("");
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        serialRead(Portsensor, "002A", "01");
        appendToTextEdit(Read, "电流标定2", "");
    });

    // 读取电流标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_33->setText("");
        LineEditInternalCode2 = ui->lineEdit_33;
        serialRead(Portsensor, "0044", "02");
        appendToTextEdit(Read, "电流标定内码2", "");
    });

    // 读取电流标定点3电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_40->setText("");
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        serialRead(Portsensor, "002B", "01");
        appendToTextEdit(Read, "电流标定点3", "");
    });

    // 读取电流标定点3对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_34->setText("");
        LineEditInternalCode3 = ui->lineEdit_34;
        serialRead(Portsensor, "0046", "02");
        appendToTextEdit(Read, "电流标定内码3", "");
    });

    // 读取电流标定点4电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_39->setText("");
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        serialRead(Portsensor, "002C", "01");
        appendToTextEdit(Read, "电流标定点4", "");
    });

    // 读取电流标定点4对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_35->setText("");
        LineEditInternalCode4 = ui->lineEdit_35;
        serialRead(Portsensor, "0048", "02");
        appendToTextEdit(Read, "电流标定内码4", "");
    });

    // 读取电流标定点5电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_38->setText("");
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        serialRead(Portsensor, "002D", "01");
        appendToTextEdit(Read, "电流标定5", "");
    });

    // 读取电流标定点5对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_36->setText("");
        LineEditInternalCode5 = ui->lineEdit_36;
        serialRead(Portsensor, "004A", "02");
        appendToTextEdit(Read, "电流标定内码5", "");
    });

    // 读传感器标定点数
    functionQueue.enqueue([this]() {
        ui->lineEdit_14->setText("");
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });

    // 读传感器标定点1Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_67->setText("");
        LineEditSensorCalibration1 = ui->lineEdit_67;
        serialRead(Portsensor, "0032", "02");
        appendToTextEdit(Read, "传感器标定点1", "");
    });

    // 读传感器标定点1内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_62->setText("");
        LineEditSensorInternalCode1 = ui->lineEdit_62;
        serialRead(Portsensor, "0053", "02");
        appendToTextEdit(Read, "传感器标定点1内码", "");
    });

    // 读传感器标定点2Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_68->setText("");
        LineEditSensorCalibration2 = ui->lineEdit_68;
        serialRead(Portsensor, "0034", "02");
        appendToTextEdit(Read, "传感器标定点2", "");
    });

    // 读传感器标定点2内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_63->setText("");
        LineEditSensorInternalCode2 = ui->lineEdit_63;
        serialRead(Portsensor, "0055", "02");
        appendToTextEdit(Read, "传感器标定点2内码", "");
    });

    // 读传感器标定点3Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_69->setText("");
        LineEditSensorCalibration3 = ui->lineEdit_69;
        serialRead(Portsensor, "0036", "02");
        appendToTextEdit(Read, "传感器标定点3", "");
    });

    // 读传感器标定点3内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_64->setText("");
        LineEditSensorInternalCode3 = ui->lineEdit_64;
        serialRead(Portsensor, "0057", "02");
        appendToTextEdit(Read, "传感器标定点3内码", "");
    });

    // 读传感器标定点4Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_70->setText("");
        LineEditSensorCalibration4 = ui->lineEdit_70;
        serialRead(Portsensor, "0038", "02");
        appendToTextEdit(Read, "传感器标定点4", "");
    });

    // 读传感器标定点4内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_65->setText("");
        LineEditSensorInternalCode4 = ui->lineEdit_65;
        serialRead(Portsensor, "0059", "02");
        appendToTextEdit(Read, "传感器标定点4内码", "");
    });

    // 读传感器标定点5Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_71->setText("");
        LineEditSensorCalibration5 = ui->lineEdit_71;
        serialRead(Portsensor, "003A", "02");
        appendToTextEdit(Read, "传感器标定点5", "");
    });

    // 读传感器标定点5内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_66->setText("");
        LineEditSensorInternalCode5 = ui->lineEdit_66;
        serialRead(Portsensor, "005B", "02");
        appendToTextEdit(Read, "传感器标定点5内码", "");
        // 可以读取
        ui->pushButton_37->setEnabled(true);
        // 开启自动扫描定时器
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });

    delay(3000);
    if (ui->pushButton_37->isEnabled()) {
        return;
    }
    ui->pushButton_37->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 读取传感器配置
void MainWindow::on_pushButton_33_clicked()
{
    // 关闭自动扫描定时器
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }

    // 清除队列
    delay(10);
    queueClear();
    delay(10);

    ui->pushButton_33->setEnabled(false);

    // 插队读传感器型号
    functionQueue.enqueue([this]() {
        isSensor = true;
        serialRead(Portsensor, "0061", "01");
        appendToTextEdit(Read, "传感器型号", "");
    });

    // 插队读传感器最小输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_17->setText("");
        LineEditOutMinPa = ui->lineEdit_17;
        serialRead(Portsensor, "006A", "02");
        appendToTextEdit(Read, "传感器输出最小值（pa）", "");
    });

    // 插队读传感器最大输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_18->setText("");
        LineEditOutMaxPa = ui->lineEdit_18;
        serialRead(Portsensor, "006C", "02");
        appendToTextEdit(Read, "传感器输出最大值（pa）", "");
    });

    // 插队读传感器最小输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_19->setText("");
        LineEditOutMincmH2O = ui->lineEdit_19;
        serialRead(Portsensor, "0066", "02");
        appendToTextEdit(Read, "传感器输出最小值（cmH2O）", "");
    });

    // 插队读传感器最大输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_20->setText("");
        LineEditOutMaxcmH2O = ui->lineEdit_20;
        serialRead(Portsensor, "0068", "02");
        appendToTextEdit(Read, "传感器输出最大值（cmH2O）", "");
    });

    // 插队读传感器最小内码值
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_21->setText("");
        LineEditMinInternalCode = ui->lineEdit_21;
        serialRead(Portsensor, "0062", "02");
        appendToTextEdit(Read, "传感器输出最小内码值", "");
    });

    // 插队读传感器满度内码值
    functionQueue.enqueue([this]() {
        ui->lineEdit_22->setText("");
        LineEditMaxInternalCode = ui->lineEdit_22;
        serialRead(Portsensor, "0064", "02");
        appendToTextEdit(Read, "传感器输出满度内码值", "");
    });

    // 插队读取传感器变送起点uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_23->setText("");
        LineEditStartPointuA = ui->lineEdit_23;
        serialRead(Portsensor, "001E", "01");
        appendToTextEdit(Read, "传感器变送起点（uA/mV）", "");
    });

    // 插队读取传感器变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_25->setText("");
        LineEditEndPointuA = ui->lineEdit_25;
        serialRead(Portsensor, "0020", "01");
        appendToTextEdit(Read, "传感器变送满度（uA/mV）", "");
    });

    // 读取传感器变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_24->setText("");
        LineEditStartPointPa = ui->lineEdit_24;
        serialRead(Portsensor, "0017", "02");
        appendToTextEdit(Read, "传感器变送起点（Pa）", "");
    });

    // 读取传感器变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_26->setText("");
        LineEditEndPointPa = ui->lineEdit_26;
        serialRead(Portsensor, "0019", "02");
        appendToTextEdit(Read, "传感器变送满度（Pa）", "");
        ui->pushButton_33->setEnabled(true);
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });

    delay(2000);
    if (ui->pushButton_33->isEnabled()) {
        return;
    }
    ui->pushButton_33->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 写入变送起点和变送满度
void MainWindow::on_pushButton_34_clicked()
{
    // 插队写入变送起点uA
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
        serialWrite(Portsensor, "001E", hexStr);
    });

    // 插队写入变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        appendToTextEdit(Write, "变送起点（Pa）", ui->lineEdit_24->text());
        serialWrite(Portsensor, "0017", ui->lineEdit_24->text());
    });

    // 插队写入变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
        serialWrite(Portsensor, "0020", hexStr);
    });

    // 插队写入变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        appendToTextEdit(Write, "变送满度（Pa）", ui->lineEdit_26->text());
        serialWrite(Portsensor, "0019", ui->lineEdit_26->text());
    });
}

// 占空比1设置
void MainWindow::on_pushButton_38_clicked()
{
    // 插队设置占空比
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_27->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比1", ui->lineEdit_27->text());
        serialWrite(Portsensor, "0028", hexStr);
    });
}

// 占空比2设置
void MainWindow::on_pushButton_39_clicked()
{
    // 插队设置占空比
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_28->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比2", ui->lineEdit_28->text());
        serialWrite(Portsensor, "0028", hexStr);
    });
}

// 占空比3设置
void MainWindow::on_pushButton_40_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_29->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比3", ui->lineEdit_29->text());
        serialWrite(Portsensor, "0028", hexStr);
    });
}

// 占空比4设置
void MainWindow::on_pushButton_41_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_30->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比4", ui->lineEdit_30->text());
        serialWrite(Portsensor, "0028", hexStr);
    });
}

// 占空比5设置
void MainWindow::on_pushButton_42_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_31->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比5", ui->lineEdit_31->text());
        serialWrite(Portsensor, "0028", hexStr);
    });
}

// 电流标定点1
void MainWindow::on_pushButton_46_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_37->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点1", ui->lineEdit_37->text());
        serialWrite(Portsensor, "0029", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });
}

// 电流标定点2
void MainWindow::on_pushButton_45_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_41->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点2", ui->lineEdit_41->text());
        serialWrite(Portsensor, "002A", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

}

// 电流标定点3
void MainWindow::on_pushButton_44_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_40->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点3", ui->lineEdit_40->text());
        serialWrite(Portsensor, "002B", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

}

// 电流标定点4
void MainWindow::on_pushButton_47_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_39->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点4", ui->lineEdit_39->text());
        serialWrite(Portsensor, "002C", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

}

// 电流标定点5
void MainWindow::on_pushButton_43_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_38->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点5", ui->lineEdit_38->text());
        serialWrite(Portsensor, "002D", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

}

// 清除电流标定点
void MainWindow::on_pushButton_48_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "清除电流标定点", "清除");
            serialWrite(Portsensor, "000C", "CD43");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "清除电流标定点", "清除");
            serialWrite(Portsensor, "00C0", "CD43");
        });
    }
}

// 读取电流标定点
void MainWindow::on_pushButton_49_clicked()
{
    // 关闭自动扫描定时器
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }

    // 清除队列
    delay(10);
    queueClear();
    delay(10);

    ui->pushButton_49->setEnabled(false);
    ui->pushButton_25->setEnabled(false);

    // 读取电流标定点数
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_13->setText("");
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 读取电流标定点1电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_37->setText("");
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        serialRead(Portsensor, "0029", "01");
        appendToTextEdit(Read, "电流标定点1", "");
    });

    // 读取电流标定点1对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_32->setText("");
        LineEditInternalCode1 = ui->lineEdit_32;
        serialRead(Portsensor, "0042", "02");
        appendToTextEdit(Read, "电流标定内码1", "");
    });

    // 读取电流标定点2电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_41->setText("");
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        serialRead(Portsensor, "002A", "01");
        appendToTextEdit(Read, "电流标定2", "");
    });

    // 读取电流标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_33->setText("");
        LineEditInternalCode2 = ui->lineEdit_33;
        serialRead(Portsensor, "0044", "02");
        appendToTextEdit(Read, "电流标定内码2", "");
    });

    // 读取电流标定点3电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_40->setText("");
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        serialRead(Portsensor, "002B", "01");
        appendToTextEdit(Read, "电流标定点3", "");
    });

    // 读取电流标定点3对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_34->setText("");
        LineEditInternalCode3 = ui->lineEdit_34;
        serialRead(Portsensor, "0046", "02");
        appendToTextEdit(Read, "电流标定内码3", "");
    });

    // 读取电流标定点4电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_39->setText("");
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        serialRead(Portsensor, "002C", "01");
        appendToTextEdit(Read, "电流标定点4", "");
    });

    // 读取电流标定点4对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_35->setText("");
        LineEditInternalCode4 = ui->lineEdit_35;
        serialRead(Portsensor, "0048", "02");
        appendToTextEdit(Read, "电流标定内码4", "");
    });

    // 读取电流标定点5电流uA
    functionQueue.enqueue([this]() {
        ui->lineEdit_38->setText("");
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        serialRead(Portsensor, "002D", "01");
        appendToTextEdit(Read, "电流标定5", "");
    });

    // 读取电流标定点5对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        ui->lineEdit_36->setText("");
        LineEditInternalCode5 = ui->lineEdit_36;
        serialRead(Portsensor, "004A", "02");
        appendToTextEdit(Read, "电流标定内码5", "");
        ui->pushButton_49->setEnabled(true);
        ui->pushButton_25->setEnabled(true);
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });

    delay(2000);
    if (ui->pushButton_49->isEnabled()) {
        return;
    }
    ui->pushButton_49->setEnabled(true);
    ui->pushButton_25->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 保存电流标定点
void MainWindow::on_pushButton_50_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "保存电流标定点", "保存");
            serialWrite(Portsensor, "000C", "AD53");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "保存电流标定点", "保存");
            serialWrite(Portsensor, "00C0", "AD53");
        });
    }
}

// 传感器标定点1
void MainWindow::on_pushButton_69_clicked()
{
    // 插队设置传感器标定点1
    functionQueue.prepend([this]() {
        isFloat = true;
        appendToTextEdit(Write, "传感器标定点1", ui->lineEdit_67->text());
        serialWrite(Portsensor, "0032", ui->lineEdit_67->text());
    });

    // 标定内码延时
    delay(100);

    // 读取传感器标定点1对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0053", "02");
        LineEditSensorInternalCode1 = ui->lineEdit_62;
        appendToTextEdit(Read, "传感器标定点1内码", "");
    });

    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });
}

// 传感器标定点2
void MainWindow::on_pushButton_70_clicked()
{
    // 插队设置传感器标定点2
    functionQueue.prepend([this]() {
        isFloat = true;
        appendToTextEdit(Write, "传感器标定点2", ui->lineEdit_68->text());
        serialWrite(Portsensor, "0034", ui->lineEdit_68->text());
    });

    // 标定内码延时
    delay(100);

    // 读取传感器标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0055", "02");
        LineEditSensorInternalCode2 = ui->lineEdit_63;

        appendToTextEdit(Read, "传感器标定点2内码", "");
    });

    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });
}

// 传感器标定点3
void MainWindow::on_pushButton_71_clicked()
{
    // 插队设置传感器标定点2
    functionQueue.prepend([this]() {
        isFloat = true;
        appendToTextEdit(Write, "传感器标定点3", ui->lineEdit_69->text());
        serialWrite(Portsensor, "0036", ui->lineEdit_69->text());
    });

    // 标定内码延时
    delay(100);

    // 读取传感器标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0057", "02");
        LineEditSensorInternalCode3 = ui->lineEdit_64;
        appendToTextEdit(Read, "传感器标定点3内码", "");
    });

    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });
}

// 传感器标定点4
void MainWindow::on_pushButton_72_clicked()
{
    // 插队设置传感器标定点4
    functionQueue.prepend([this]() {
        isFloat = true;
        appendToTextEdit(Write, "传感器标定点4", ui->lineEdit_70->text());
        serialWrite(Portsensor, "0038", ui->lineEdit_70->text());
    });

    // 标定内码延时
    delay(100);

    // 读取传感器标定点4对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0059", "02");
        LineEditSensorInternalCode4 = ui->lineEdit_65;
        appendToTextEdit(Read, "传感器标定点4内码", "");
    });

    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });
}

// 传感器标定点5
void MainWindow::on_pushButton_73_clicked()
{
    // 插队设置传感器标定点5
    functionQueue.prepend([this]() {
        isFloat = true;
        appendToTextEdit(Write, "传感器标定点5", ui->lineEdit_71->text());
        serialWrite(Portsensor, "003A", ui->lineEdit_71->text());
    });

    // 标定内码延时
    delay(100);

    // 读取传感器标定点5对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "005B", "02");
        LineEditSensorInternalCode5 = ui->lineEdit_66;
        appendToTextEdit(Read, "传感器标定点5内码", "");
    });

    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });
}

// 清除传感器标定
void MainWindow::on_pushButton_74_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "清除传感器标定点", "清除");
            serialWrite(Portsensor, "000C", "9D63");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "清除传感器标定点", "清除");
            serialWrite(Portsensor, "00C0", "9D63");
        });
    }
}

// 读取传感器标定
void MainWindow::on_pushButton_75_clicked()
{
    // 关闭自动扫描定时器
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }

    // 清除队列
    delay(10);
    queueClear();
    delay(10);

    ui->pushButton_75->setEnabled(false);

    // 读传感器标定点数
    functionQueue.enqueue([this]() {
        ui->lineEdit_14->setText("");
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });

    // 读传感器标定点1Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_67->setText("");
        LineEditSensorCalibration1 = ui->lineEdit_67;
        serialRead(Portsensor, "0032", "02");
        appendToTextEdit(Read, "传感器标定点1", "");
    });

    // 读传感器标定点1内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_62->setText("");
        LineEditSensorInternalCode1 = ui->lineEdit_62;
        serialRead(Portsensor, "0053", "02");
        appendToTextEdit(Read, "传感器标定点1内码", "");
    });

    // 读传感器标定点2Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_68->setText("");
        LineEditSensorCalibration2 = ui->lineEdit_68;
        serialRead(Portsensor, "0034", "02");
        appendToTextEdit(Read, "传感器标定点2", "");
    });

    // 读传感器标定点2内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_63->setText("");
        LineEditSensorInternalCode2 = ui->lineEdit_63;
        serialRead(Portsensor, "0055", "02");
        appendToTextEdit(Read, "传感器标定点2内码", "");
    });

    // 读传感器标定点3Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_69->setText("");
        LineEditSensorCalibration3 = ui->lineEdit_69;
        serialRead(Portsensor, "0036", "02");
        appendToTextEdit(Read, "传感器标定点3", "");
    });

    // 读传感器标定点3内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_64->setText("");
        LineEditSensorInternalCode3 = ui->lineEdit_64;
        serialRead(Portsensor, "0057", "02");
        appendToTextEdit(Read, "传感器标定点3内码", "");
    });

    // 读传感器标定点4Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_70->setText("");
        LineEditSensorCalibration4 = ui->lineEdit_70;
        serialRead(Portsensor, "0038", "02");
        appendToTextEdit(Read, "传感器标定点4", "");
    });

    // 读传感器标定点4内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_65->setText("");
        LineEditSensorInternalCode4 = ui->lineEdit_65;
        serialRead(Portsensor, "0059", "02");
        appendToTextEdit(Read, "传感器标定点4内码", "");
    });

    // 读传感器标定点5Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        ui->lineEdit_71->setText("");
        LineEditSensorCalibration5 = ui->lineEdit_71;
        serialRead(Portsensor, "003A", "02");
        appendToTextEdit(Read, "传感器标定点5", "");
    });

    // 读传感器标定点5内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        ui->lineEdit_66->setText("");
        LineEditSensorInternalCode5 = ui->lineEdit_66;
        serialRead(Portsensor, "005B", "02");
        appendToTextEdit(Read, "传感器标定点5内码", "");
        // 可以读取
        ui->pushButton_75->setEnabled(true);
        // 开启自动扫描定时器
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });

    delay(2000);
    if (ui->pushButton_75->isEnabled()) {
        return;
    }
    ui->pushButton_75->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 保存传感器标定
void MainWindow::on_pushButton_76_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "保存传感器标定点", "保存");
            serialWrite(Portsensor, "000C", "8D73");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "保存传感器标定点", "保存");
            serialWrite(Portsensor, "00C0", "8D73");
        });
    }
}

// 自动扫描
void MainWindow::on_pushButton_22_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::critical(this, "错误", "请先打开串口");
        return;
    }

    // 关闭上次自动扫描
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
        isAutoScan = false;
        ui->pushButton_22->setStyleSheet("");
        ui->pushButton_22->setText("扫描设备");
        return;
    }

    // 开启自动扫描
    if (ui->lineEdit_8->text().isEmpty()) {
        QMessageBox::information(this, "错误", "间隔（ms）不能为空", QMessageBox::Ok);
        return;
    } else {
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
        isAutoScan = true;
        ui->pushButton_22->setStyleSheet("background-color: #00BFFF;");
        ui->pushButton_22->setText("关闭扫描");
    }
}

// 写设备地址
void MainWindow::on_pushButton_13_clicked()
{
    // 入队
    if (!isVoltageSensor) {
        functionQueue.prepend([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_7->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "设备地址", ui->lineEdit_7->text());
            serialWrite(Portsensor, "0010", hexStr);
        });
    } else if (isVoltageSensor) {
        functionQueue.prepend([this]() {
            isDeviceID = true;
            QString hexStr = QString("%1").arg(ui->lineEdit_7->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "设备地址", ui->lineEdit_7->text());
            serialWrite(Portsensor, "0000", hexStr);
        });
    }
}

// 写波特率
void MainWindow::on_pushButton_15_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::information(this, "错误!", "请打开传感器串口");
        return;
    }
    if (!isVoltageSensor) {
        functionQueue.prepend([this]() {
            appendToTextEdit(Write, "波特率", ui->comboBox_6->currentText());
            serialWrite(Portsensor, "0011", baudMap.value(ui->comboBox_6->currentText()).rightJustified(4, '0'));
            sensorSerial.setBaudRate(ui->comboBox_6->currentText().toInt());
        });
    } else if (isVoltageSensor) {
        functionQueue.prepend([this]() {
            appendToTextEdit(Write, "波特率", ui->comboBox_6->currentText());
            serialWrite(Portsensor, "0001", voltageBaudMap.value(ui->comboBox_6->currentText()).rightJustified(4, '0'));
            sensorSerial.setBaudRate(ui->comboBox_6->currentText().toInt());
        });
    }
}

// 写电流下限
void MainWindow::on_pushButton_17_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_9->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流/电压下限", ui->lineEdit_9->text());
        serialWrite(Portsensor, "0015", hexStr);
    });
}

// 写电流上限
void MainWindow::on_pushButton_19_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.prepend([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
            serialWrite(Portsensor, "0016", hexStr);
        });
    } else {
        functionQueue.enqueue([this]() {
            ui->lineEdit_10->setText("");
            LineEditCurrentMax = ui->lineEdit_10;
            appendToTextEdit(Read, "电流/电压上限", "");
            serialRead(Portsensor, "0022", "01");
        });
    }
}

// 写零点跟踪
void MainWindow::on_pushButton_21_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_11->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "零点跟踪", ui->lineEdit_11->text());
            serialWrite(Portsensor, "001B", hexStr);
        });
    } else {
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_11->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "零点跟踪", ui->lineEdit_11->text());
            serialWrite(Portsensor, "000C", hexStr);
        });
    }
}

// 0Pa校正
void MainWindow::on_pushButton_10_clicked()
{
    // 设置0PA校正
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "0pA校正", "校正");
            serialWrite(Portsensor, "000C", "0030");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "0pA校正", "校正");
            serialWrite(Portsensor, "00C0", "0030");
        });
    }
}

// 变送模式
void MainWindow::on_pushButton_11_clicked()
{
    autoScanTimer.stop();

    // 清除队列
    delay(10);
    queueClear();
    delay(10);

    // 设置变送模式
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "变送模式", "变送模式使能");
            serialWrite(Portsensor, "000C", "4253");
        });
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "变送模式", "变送模式使能");
            serialWrite(Portsensor, "000F", "00");
        });
    }
    ui->pushButton_11->setEnabled(false);
    delay(1000);
    ui->pushButton_11->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 指针清空
void MainWindow::pointerInit()
{
    // 参数配置
    LineEditID = nullptr;
    LineEditCurrentMin = nullptr;
    LineEditCurrentMax = nullptr;
    LineEditZeroTracking = nullptr;

    // 传感器配置
    LineEditOutMinPa = nullptr;
    LineEditOutMaxPa = nullptr;
    LineEditOutMincmH2O = nullptr;
    LineEditOutMaxcmH2O = nullptr;
    LineEditMinInternalCode = nullptr;
    LineEditMaxInternalCode = nullptr;
    LineEditStartPointuA = nullptr;
    LineEditStartPointPa = nullptr;
    LineEditEndPointuA = nullptr;
    LineEditEndPointPa = nullptr;

    // 电流标定
    LineEditDutyCycle1 = nullptr;
    LineEditDutyCycle2 = nullptr;
    LineEditDutyCycle3 = nullptr;
    LineEditDutyCycle4 = nullptr;
    LineEditDutyCycle5 = nullptr;
    LineEditInternalCode1 = nullptr;
    LineEditInternalCode2 = nullptr;
    LineEditInternalCode3 = nullptr;
    LineEditInternalCode4 = nullptr;
    LineEditInternalCode5 = nullptr;
    LineEditCurrentCalibration1 = nullptr;
    LineEditCurrentCalibration2 = nullptr;
    LineEditCurrentCalibration3 = nullptr;
    LineEditCurrentCalibration4 = nullptr;
    LineEditCurrentCalibration5 = nullptr;
    LineEditCurrentCalibrationNum = nullptr;

    // 传感器标定
    LineEditSensorCalibration1 = nullptr;
    LineEditSensorCalibration2 = nullptr;
    LineEditSensorCalibration3 = nullptr;
    LineEditSensorCalibration4 = nullptr;
    LineEditSensorCalibration5 = nullptr;
    LineEditSensorInternalCode1 = nullptr;
    LineEditSensorInternalCode2 = nullptr;
    LineEditSensorInternalCode3 = nullptr;
    LineEditSensorInternalCode4 = nullptr;
    LineEditSensorInternalCode5 = nullptr;
    LineEditSensorCalibrationNum = nullptr;

    // 自动扫描配置
    LineEditPressure        = nullptr;
    LineEditInternalCode    = nullptr;
    LineEditCurrent         = nullptr;
    LineEditPWM             = nullptr;
    LineEditTemprature      = nullptr;

    // 电压传感器特有
    LineEditPoint   = nullptr;

    // ComboBox
    ComboBoxBaud = nullptr;

    // 一些bool指令
    isTemperature        = false;
    isFirmwareVersion    = false;
    isFirmwareinfo       = false;
    isSensor             = false;
    isTransmissionMethod = false;
}

// 自动出队槽函数
void MainWindow::autoDequeueSolt()
{
    if (functionQueue.isEmpty()) return;
    if (waitingResponseTimer.isActive()) return;    // 感觉有问题这里
    waitingResponseTimer.start(1000);      // 主要针对自动扫描
    auto func = functionQueue.head();    // 出队
    func();
}

// 等待回复超时时间
void MainWindow::waitingResponseTimerSolt()
{
    // 自动扫描停止
    autoScanTimer.stop();
    // 自动扫描下，数据超时会停止扫描，此时如果一键读取会自动开启自动扫描，所以需要这里变成false
    isAutoScan = false;
    ui->pushButton_22->setStyleSheet("");
    ui->pushButton_22->setText("扫描设备");
    pointerInit();
    // 窗口提示
    appendToTextEdit(Receive, "", "接收数据超时");
    waitingResponseTimer.stop();
    if (timeoutTimes < 3) {
        timeoutTimes++;
    } else {
        delay(10);
        queueClear();
        delay(10);
        timeoutTimes = 0;
    }
    if (!autoDequeueTimer.isActive()) autoDequeueTimer.start(10);      // 有时候会出现06功能码只返回7个数据，所以需要报超时,针对这个超时，要开启定时器
}

// 一键标定
void MainWindow::on_pushButton_25_clicked()
{
    // 检查万用表串口有没有打开
    if (!MultimeterSerial.isOpen()) {
        QMessageBox::critical(this, "错误", "请先打开万用表串口","");
        return;
    }
    // 关闭自动扫描
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }
    // 清除队列
    delay(10);
    queueClear();
    delay(10);
    this->setEnabled(false);

    // 先发一条指令告诉万用表我要开始读了,先清除
    delay(500);
    serialWrite(PortMultimeter, "", "*CLS");

    // 先发一条指令不填充
    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        appendToTextEdit(Read, "电流测量", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        appendToTextEdit(Read, "电压测量", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    // 占空比1
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_27->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比1", ui->lineEdit_27->text());
        serialWrite(Portsensor, "0028", hexStr);
    });

    // 修改占空比，填充电流
    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_27->text()+"% 时电流", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_27->text()+"% 时电压", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_37->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点1", ui->lineEdit_37->text());
        serialWrite(Portsensor, "0029", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        appendToTextEdit(Read, "电流标定点数", "");
        serialRead(Portsensor, "0041", "01");
    });

    // 占空比2
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_28->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比2", ui->lineEdit_28->text());
        serialWrite(Portsensor, "0028", hexStr);
    });

    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_28->text()+"% 时电流", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_28->text()+"% 时电压", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_41->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点2", ui->lineEdit_41->text());
        serialWrite(Portsensor, "002A", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        appendToTextEdit(Read, "电流标定点数", "");
        serialRead(Portsensor, "0041", "01");
    });

    // 占空比3
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_29->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比3", ui->lineEdit_29->text());
        serialWrite(Portsensor, "0028", hexStr);
    });

    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_29->text()+"% 时电流", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_29->text()+"% 时电压", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_40->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点3", ui->lineEdit_40->text());
        serialWrite(Portsensor, "002B", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        appendToTextEdit(Read, "电流标定点数", "");
        serialRead(Portsensor, "0041", "01");
    });

    // 占空比4
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_30->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比4", ui->lineEdit_30->text());
        serialWrite(Portsensor, "0028", hexStr);
    });

    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_30->text()+"% 时电流", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_30->text()+"% 时电压", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_39->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点4", ui->lineEdit_39->text());
        serialWrite(Portsensor, "002C", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        appendToTextEdit(Read, "电流标定点数", "");
        serialRead(Portsensor, "0041", "01");
    });

    // 占空比5
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_31->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "占空比5", ui->lineEdit_31->text());
        serialWrite(Portsensor, "0028", hexStr);
    });

    if (!isVoltageSensor) {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_31->text()+"% 时电流", "");
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
    } else {
        delay(ui->lineEdit_12->text().toInt());
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_30->text()+"% 时电压", "");
        serialWrite(PortMultimeter, "", ":MEASure:VOLTage:DC?");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_38->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流标定点5", ui->lineEdit_38->text());
        serialWrite(Portsensor, "002D", hexStr);
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        appendToTextEdit(Read, "电流标定点数", "");
        serialRead(Portsensor, "0041", "01");
    });

    this->setEnabled(true);
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 选传感器
void MainWindow::selectSensor(QPushButton *button)
{
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }
    // 先把按钮都变成墨绿
    for (QPushButton *btn : buttonList) {
        btn->setStyleSheet("");
    }
    appendToTextEdit(Write, "传感器型号", button->text());
    waitingResponseTimer.start(1000);
    if (!isVoltageSensor) {
        QString sensorID = sensorListMap.value(button->text());
        QStringList parts = sensorID.split(" ", QString::SkipEmptyParts);    // 去空格，然后加到字符串列表里
        QString sensorIDHex;
        for (const QString& part : parts) {
            bool ok;
            int value = part.toInt(&ok);  // 转为整数
            if (ok) {
                sensorIDHex += QString("%1").arg(value, 2, 16, QChar('0')).toUpper();  // 转为2位十六进制
            }
        }
        serialWrite(Portsensor, "0061", sensorIDHex);
    } else {
        QString sensorID = voltageSensorListMap.value(button->text());
        QStringList parts = sensorID.split(" ", QString::SkipEmptyParts);    // 去空格，然后加到字符串列表里
        QString sensorIDHex;
        for (const QString& part : parts) {
            bool ok;
            int value = part.toInt(&ok);  // 转为整数
            if (ok) {
                sensorIDHex += QString("%1").arg(value, 2, 16, QChar('0')).toUpper();  // 转为2位十六进制
            }
        }
        serialWrite(Portsensor, "0061", sensorIDHex);
    }
    delay(100);
    // 读传感器型号
    functionQueue.enqueue([this]() {
        isSensor = true;
        serialRead(Portsensor, "0061", "01");
        appendToTextEdit(Read, "传感器型号", "");
    });
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 15011 1K
void MainWindow::on_pushButton_27_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("1000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("500");
    ui->lineEdit_69->setText("1000");
    selectSensor(ui->pushButton_27);
}

// 15021 2K
void MainWindow::on_pushButton_30_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("2000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("1000");
    ui->lineEdit_69->setText("2000");
    selectSensor(ui->pushButton_30);
}

// 15061 5K
void MainWindow::on_pushButton_31_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("5000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("2500");
    ui->lineEdit_69->setText("5000");
    selectSensor(ui->pushButton_31);
}

// 15101 10K
void MainWindow::on_pushButton_51_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("10000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("5000");
    ui->lineEdit_69->setText("10000");
    selectSensor(ui->pushButton_51);
}

// 15201 20K
void MainWindow::on_pushButton_52_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("20000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("10000");
    ui->lineEdit_69->setText("20000");
    selectSensor(ui->pushButton_52);
}

// 15401 40K
void MainWindow::on_pushButton_53_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("40000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("20000");
    ui->lineEdit_69->setText("40000");
    selectSensor(ui->pushButton_53);
}

// 15111 100K
void MainWindow::on_pushButton_102_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("100000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("50000");
    ui->lineEdit_69->setText("100000");
    selectSensor(ui->pushButton_102);
}

// 54012 1K
void MainWindow::on_pushButton_106_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("1000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("500");
    ui->lineEdit_69->setText("1000");
    selectSensor(ui->pushButton_106);
}

// 54022 2K
void MainWindow::on_pushButton_105_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("2000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("1000");
    ui->lineEdit_69->setText("2000");
    selectSensor(ui->pushButton_105);
}

// 54042 4K
void MainWindow::on_pushButton_104_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("4000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("2000");
    ui->lineEdit_69->setText("4000");
    selectSensor(ui->pushButton_104);
}

// 54102 10K
void MainWindow::on_pushButton_103_clicked()
{
    ui->lineEdit_24->setText("0");
    ui->lineEdit_26->setText("10000");
    ui->lineEdit_67->setText("0");
    ui->lineEdit_68->setText("5000");
    ui->lineEdit_69->setText("10000");
    selectSensor(ui->pushButton_103);
}

// 写入Pa
void MainWindow::on_pushButton_108_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            setpressureUnit = true;
            appendToTextEdit(Write, "压力单位", "Pa");
            serialWrite(Portsensor, "0012", "00");
        });
    } else {
        functionQueue.enqueue([this]() {
            setpressureUnit = true;
            appendToTextEdit(Write, "压力单位", "Pa");
            serialWrite(Portsensor, "0002", "02");
        });
    }
}

// 写入kPa
void MainWindow::on_pushButton_109_clicked()
{
    if (!isVoltageSensor) {
        functionQueue.enqueue([this]() {
            setpressureUnit = true;
            appendToTextEdit(Write, "压力单位", "kPa");
            serialWrite(Portsensor, "0012", "01");
        });
    } else {
        functionQueue.enqueue([this]() {
            setpressureUnit = true;
            appendToTextEdit(Write, "压力单位", "kPa");
            serialWrite(Portsensor, "0002", "01");
        });
    }
}

// 设置变送方式
void MainWindow::setTransmissionMethod(QPushButton *button)
{
    // 关闭自动扫描定时器
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }

    // 自动出队会异常关闭，在这里打开一下
    if (autoDequeueTimer.isActive()) {

    } else {
        pointerInit();
        waitingResponseTimer.stop();        // 有时候会出现06功能码只返回7个数据，所以需要报超时
        if (!functionQueue.isEmpty()) {     // 选择传感器型号时，没有用队列发，此时队列时空的，不能出队
            functionQueue.dequeue();
        }
        autoDequeueTimer.start(10);
    };

    queueClear();

    // 先把三个按钮全换成墨绿色
    ui->pushButton_26->setStyleSheet("");
    ui->pushButton_28->setStyleSheet("");
    ui->pushButton_29->setStyleSheet("");
    for (QPushButton *btn : buttonList) {
        pushButtonDisEnable(btn);
    };

    // 变送方式
    if (button->text() == "4mA~20mA") {
        functionQueue.enqueue([this]() {
            ui->pushButton_26->setStyleSheet("background-color: #00BFFF;");
            appendToTextEdit(Write, "变送方式", "4mA~20mA");
            serialWrite(Portsensor, "0013", "00");
        });

        delay(100);     // 写入变送方式后立刻写入变送起点会返回差错码，需要加延时

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
            serialWrite(Portsensor, "001E", hexStr);
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
            serialWrite(Portsensor, "0020", hexStr);
        });

        // 点击按钮后，波特率列表会初始化，所以需要读波特率
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            appendToTextEdit(Read, "波特率", "");
            serialRead(Portsensor, "0011", "01");
        });

        // 设置电流输出最大值
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
            serialWrite(Portsensor, "0016", hexStr);
        });
    } else if (button->text() == "0V~5V") {
        functionQueue.enqueue([this]() {
            ui->pushButton_28->setStyleSheet("background-color: #00BFFF;");
            appendToTextEdit(Write, "变送方式", "0V~5V");
            serialWrite(Portsensor, "0013", "01");
        });

        delay(100);     // 写入变送方式后立刻写入变送起点会返回差错码，需要加延时

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
            serialWrite(Portsensor, "001E", hexStr);
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
            serialWrite(Portsensor, "0020", hexStr);
        });

        // 点击按钮后，波特率列表会初始化，所以需要读波特率
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0001", "01");
            appendToTextEdit(Read, "波特率", "");
        });

        // 设置电流输出最大值
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
            serialWrite(Portsensor, "0022", hexStr);
        });
    } else if (button->text() == "0V~10V") {
        functionQueue.enqueue([this]() {
            ui->pushButton_29->setStyleSheet("background-color: #00BFFF;");
            appendToTextEdit(Write, "变送方式", "0V~10V");
            serialWrite(Portsensor, "0013", "02");
        });

        delay(100);     // 写入变送方式后立刻写入变送起点会返回差错码，需要加延时

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
            serialWrite(Portsensor, "001E", hexStr);
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
            serialWrite(Portsensor, "0020", hexStr);
        });

        // 点击按钮后，波特率列表会初始化，所以需要读波特率
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            appendToTextEdit(Read, "波特率", "");
            serialRead(Portsensor, "0001", "01");
        });

        // 设置电流输出最大值
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
            serialWrite(Portsensor, "0022", hexStr);
        });
    }

    // 读取变送方式
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0013", "01");
        appendToTextEdit(Read, "变送方式", "");
    });

    // 设置满度信号
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "满度信号", ui->lineEdit_25->text());
        serialWrite(Portsensor, "0020", hexStr);
    });

    // 设置起点信号
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "传感器变送起点（uA/mV）", ui->lineEdit_23->text());
        serialWrite(Portsensor, "001E", hexStr);
    });

    // 设置电流输出最小值
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_9->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        appendToTextEdit(Write, "电流/电压下限", ui->lineEdit_9->text());
        serialWrite(Portsensor, "0015", hexStr);
    });

    // 重新打开自动扫描定时器
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 当前状态
void MainWindow::currentTransmissionMethod(const QString &arg1)
{
    if (arg1 == "0") {         // 4mA~20mA
        // 参数配置修改
        ui->pushButton_35->hide();
        ui->pushButton_36->hide();
        ui->lineEdit_16->hide();
        ui->pushButton_23->hide();
        ui->label_16->hide();
        ui->comboBox_4->hide();
        ui->pushButton_24->hide();
        ui->pushButton_32->hide();

        ui->label->setText("电流");
        ui->label_9 ->setText("电流下限(uA)");
        ui->label_10->setText("电流上限(uA)");
        ui->lineEdit_9 ->setText("3800");
        ui->lineEdit_10->setText("20200");

        isVoltageSensor = false;

        // 传感器配置修改
        ui->label->setText("电流");
        ui->label_25->setText("uA");
        ui->label_28->setText("uA");
        ui->lineEdit_23->setText("4000");
        ui->lineEdit_25->setText("20000");

        // 电流标定修改
        ui->label_31->setText("           0~100   内码   uA");
        ui->pushButton_26->setStyleSheet("background-color: #00BFFF;");     // 蓝色
        ui->pushButton_28->setStyleSheet("");
        ui->pushButton_29->setStyleSheet("");
        pushButtonDisEnable(ui->pushButton_13);
        pushButtonDisEnable(ui->pushButton_15);
    } else if (arg1 == "1") {   // 0V~5V
        // 参数配置修改
        // ui->pushButton_35->show();
        // ui->pushButton_36->show();
        ui->pushButton_35->hide();      // 1010隐藏
        ui->pushButton_36->hide();      // 1010隐藏

        ui->lineEdit_16->show();
        ui->pushButton_23->show();
        ui->label_16->show();
        ui->comboBox_4->show();
        ui->pushButton_24->show();
        ui->pushButton_32->show();

        ui->label_9 ->setText("电压下限(mV)");
        ui->label_10->setText("电压上限(mV)");
        ui->lineEdit_9 ->setText("0");
        ui->lineEdit_10->setText("6000");

        isVoltageSensor = true;

        // 传感器配置修改
        ui->label->setText("电压");
        ui->label_25->setText("mV");
        ui->label_28->setText("mV");
        ui->lineEdit_23->setText("0");
        ui->lineEdit_25->setText("5000");

        // 电流标定修改
        ui->label_31->setText("           0~100   内码   mV");
        ui->pushButton_28->setStyleSheet("background-color: #00BFFF;");
        ui->pushButton_26->setStyleSheet("");
        ui->pushButton_29->setStyleSheet("");

        // 按钮失效
        pushButtonEnable(ui->pushButton_13);
        pushButtonEnable(ui->pushButton_15);
    } else if (arg1 == "2") {  // 0V~10V
        // 参数配置修改
        // ui->pushButton_35->show();
        // ui->pushButton_36->show();
        ui->pushButton_35->hide();      // 1010隐藏
        ui->pushButton_36->hide();      // 1010隐藏
        ui->lineEdit_16->show();
        ui->pushButton_23->show();
        ui->label_16->show();
        ui->comboBox_4->show();
        ui->pushButton_24->show();
        ui->pushButton_32->show();

        ui->label_9 ->setText("电压下限(mV)");
        ui->label_10->setText("电压上限(mV)");
        ui->lineEdit_9 ->setText("0");
        ui->lineEdit_10->setText("11000");

        isVoltageSensor = true;

        // 传感器配置修改
        ui->label->setText("电压");
        ui->label_25->setText("mV");
        ui->label_28->setText("mV");
        ui->lineEdit_23->setText("0");
        ui->lineEdit_25->setText("10000");

        // 电流标定修改
        ui->label_31->setText("           0~100   内码   mV");
        ui->pushButton_29->setStyleSheet("background-color: #00BFFF;");
        ui->pushButton_28->setStyleSheet("");
        ui->pushButton_26->setStyleSheet("");

        // 按钮使能
        pushButtonEnable(ui->pushButton_13);
        pushButtonEnable(ui->pushButton_15);
    }
}

void MainWindow::queueClear()
{
    functionQueue.clear();
    pointerInit();
    data.clear();
    ui->pushButton_37->setEnabled(true);
    ui->pushButton_33->setEnabled(true);
    ui->pushButton_49->setEnabled(true);
    ui->pushButton_75->setEnabled(true);
}

void MainWindow::pushButtonDisEnable(QPushButton *button)
{
    button->setStyleSheet("background-color: #808080;");        // 灰色
    button->setEnabled(false);
}

void MainWindow::pushButtonEnable(QPushButton *button)
{
    button->setStyleSheet("");
    button->setEnabled(true);
}

// 选择变送方式为4mA~20mA
void MainWindow::on_pushButton_26_clicked()
{
    // 隐藏校正数据
    ui->lineEdit_12->setText("1000");

    ui->pushButton_35->hide();
    ui->pushButton_36->hide();
    ui->lineEdit_16->hide();
    ui->pushButton_23->hide();

    ui->label_9 ->setText("电流下限(uA)");
    ui->label_10->setText("电流上限(uA)");
    ui->lineEdit_9 ->setText("3800");
    ui->lineEdit_10->setText("20200");

    isVoltageSensor = false;

    // 传感器配置修改
    ui->label->setText("电压");
    ui->label_25->setText("uA");
    ui->label_28->setText("uA");
    ui->lineEdit_23->setText("4000");
    ui->lineEdit_25->setText("20000");

    // 波特率修改
    ui->comboBox_6->clear();
    for (auto it = baudMap.constBegin(); it != baudMap.constEnd(); ++it) {
        ui->comboBox_6->addItem(it.key());  // 显示型号，绑定ID
    }

    // 电流标定修改
    ui->label_31->setText("           0~100   内码   uA");
    ui->lineEdit_27->setText("17");
    ui->lineEdit_28->setText("30");
    ui->lineEdit_29->setText("50");
    ui->lineEdit_30->setText("75");
    ui->lineEdit_31->setText("85");
    setTransmissionMethod(ui->pushButton_26);
}

// 选择变送方式为0V~5V
void MainWindow::on_pushButton_28_clicked()
{
    // 参数配置修改
    ui->lineEdit_12->setText("2000");

    // ui->pushButton_35->show();
    // ui->pushButton_36->show();
    ui->pushButton_35->hide();      // 1010隐藏
    ui->pushButton_36->hide();      // 1010隐藏
    ui->lineEdit_16->show();
    ui->pushButton_23->show();

    ui->label_9 ->setText("电压下限(mV)");
    ui->label_10->setText("电压上限(mV)");
    ui->lineEdit_9 ->setText("0");
    ui->lineEdit_10->setText("6000");

    isVoltageSensor = true;

    // 传感器配置修改
    ui->label->setText("电压");
    ui->label_25->setText("mV");
    ui->label_28->setText("mV");
    ui->lineEdit_23->setText("0");
    ui->lineEdit_25->setText("5000");

    // 波特率修改
    ui->comboBox_6->clear();
    for (auto it = voltageBaudMap.constBegin(); it != voltageBaudMap.constEnd(); ++it) {
        ui->comboBox_6->addItem(it.key());  // 显示型号，绑定ID
    }

    // 电流标定修改
    ui->label_31->setText("           0~100   内码   mV");
    ui->lineEdit_27->setText("5");
    ui->lineEdit_28->setText("15");
    ui->lineEdit_29->setText("35");
    ui->lineEdit_30->setText("50");
    ui->lineEdit_31->setText("70");
    setTransmissionMethod(ui->pushButton_28);
}

// 选择变送方式为0V~10V
void MainWindow::on_pushButton_29_clicked()
{
    ui->lineEdit_12->setText("2000");

    // ui->pushButton_35->show();
    // ui->pushButton_36->show();
    ui->pushButton_35->hide();      // 1010隐藏
    ui->pushButton_36->hide();      // 1010隐藏
    ui->lineEdit_16->show();
    ui->pushButton_23->show();

    ui->label_9 ->setText("电压下限(mV)");
    ui->label_10->setText("电压上限(mV)");
    ui->lineEdit_9 ->setText("0");
    ui->lineEdit_10->setText("11000");

    isVoltageSensor = true;

    // 传感器配置修改
    ui->label->setText("电压");
    ui->label_25->setText("mV");
    ui->label_28->setText("mV");
    ui->lineEdit_23->setText("0");
    ui->lineEdit_25->setText("10000");

    // 波特率修改
    ui->comboBox_6->clear();
    for (auto it = voltageBaudMap.constBegin(); it != voltageBaudMap.constEnd(); ++it) {
        ui->comboBox_6->addItem(it.key());  // 显示型号，绑定ID
    }

    // 电流标定修改
    ui->label_31->setText("           0~100   内码   mV");
    ui->lineEdit_27->setText("5");
    ui->lineEdit_28->setText("15");
    ui->lineEdit_29->setText("35");
    ui->lineEdit_30->setText("50");
    ui->lineEdit_31->setText("70");
    setTransmissionMethod(ui->pushButton_29);
}

// 读小数点位
void MainWindow::on_pushButton_23_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::information(this, "错误!", "请打开传感器串口");
        return;
    }

    if (!isVoltageSensor) {
        QMessageBox::information(this, "错误!", "请选择电压型传感器");
        return;
    }
    functionQueue.enqueue([this]() {
        LineEditPoint = ui->lineEdit_16;
        serialRead(Portsensor, "0003", "01");
        appendToTextEdit(Read, "小数点位", "");
    });
}

// 读串口校验位
void MainWindow::on_pushButton_24_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::information(this, "错误!", "请打开传感器串口");
        return;
    }

    if (!isVoltageSensor) {
        QMessageBox::information(this, "错误!", "请选择电压型传感器");
        return;
    }
    functionQueue.enqueue([this]() {
        ComboBoxCheck = ui->comboBox_4;
        serialRead(Portsensor, "0025", "01");
        appendToTextEdit(Read, "校验位", "");
    });
}

// 写串口校验位
void MainWindow::on_pushButton_32_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::information(this, "错误!", "请打开传感器串口");
        return;
    }

    if (!isVoltageSensor) {
        if (ui->comboBox_4->currentText() == "无校验") {
            sensorSerial.setParity(QSerialPort::NoParity);
        } else if (ui->comboBox_4->currentText() == "奇校验") {
            sensorSerial.setParity(QSerialPort::OddParity);
        } else if (ui->comboBox_4->currentText() == "偶校验") {
            sensorSerial.setParity(QSerialPort::EvenParity);
        }
        return;
    } else {
        functionQueue.enqueue([this]() {
            appendToTextEdit(Write, "写校验位", ui->comboBox_4->currentText());
            QString str = QString::number(ui->comboBox_4->currentIndex());
            serialWrite(Portsensor, "0025", str);
            if (ui->comboBox_4->currentText() == "无校验") {
                sensorSerial.setParity(QSerialPort::NoParity);
            } else if (ui->comboBox_4->currentText() == "奇校验") {
                sensorSerial.setParity(QSerialPort::OddParity);
            } else if (ui->comboBox_4->currentText() == "偶校验") {
                sensorSerial.setParity(QSerialPort::EvenParity);
            }
        });
    }
}

// 连接设备
void MainWindow::on_pushButton_54_clicked()
{
    if (!sensorSerial.isOpen()) {
        QMessageBox::critical(this, "错误", "请先打开串口");
        return;
    }

    // 自动出队会异常关闭，在这里打开一下
    if (autoDequeueTimer.isActive()) {

    } else {
        pointerInit();
        waitingResponseTimer.stop();        // 有时候会出现06功能码只返回7个数据，所以需要报超时
        if (!functionQueue.isEmpty()) {     // 选择传感器型号时，没有用队列发，此时队列时空的，不能出队
            functionQueue.dequeue();
        }
        autoDequeueTimer.start(10);
    };

    //  连接设备时先变回来
    ui->pushButton_26->setStyleSheet("");
    ui->pushButton_28->setStyleSheet("");
    ui->pushButton_29->setStyleSheet("");

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("提示");
    msgBox.setText("请选择变送方式！");

    // 自定义按钮
    QPushButton *btnCurrent = msgBox.addButton("电流传感器", QMessageBox::YesRole);
    QPushButton *btnVoltage = msgBox.addButton("电压传感器", QMessageBox::NoRole);

    // 加一个 Cancel 按钮（让 X / ESC 能捕捉到）
    QPushButton *btnCancel = msgBox.addButton(QMessageBox::Cancel);
    btnCancel->setVisible(false);  // 隐藏掉，不显示给用户看
    btnCurrent->setFixedSize(120, 35);
    btnVoltage->setFixedSize(120, 35);
    msgBox.setMinimumSize(340, 140);
    QFont btnFont;
    btnFont.setPointSize(14);
    btnCurrent->setFont(btnFont);
    btnVoltage->setFont(btnFont);
    msgBox.exec();

    if (msgBox.clickedButton() == btnCurrent) {
        isVoltageSensor = false;

        // 读取固件版本
        functionQueue.enqueue([this]() {
            serialRead(Portsensor, "0000", "01");
            isFirmwareVersion = true;
            appendToTextEdit(Read, "读固件版本", "");
        });

        // 读固件信息
        functionQueue.enqueue([this]() {
            serialRead(Portsensor, "0001", "02");
            isFirmwareinfo = true;
            appendToTextEdit(Read, "读固件信息", "");
        });
    } else if (msgBox.clickedButton() == btnVoltage) {
        isVoltageSensor = true;

        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            QByteArray frame;
            frame.append(static_cast<char>(LengthByte_00));
            frame.append(0x04);
            frame.append(static_cast<char>(LengthByte_00));
            frame.append(static_cast<char>(LengthByte_00));
            frame.append(static_cast<char>(LengthByte_00));
            frame.append(0x01);
            frame.append(0x30);
            frame.append(0x1B);
            sensorSerial.write(frame);
            waitingResponseTimer.start(1000);
            appendToTextEdit(Read, "开始连接设备", "");
        });
    } else {
        QMessageBox::warning(this, "提示", "你没有选择任何方式！");
        return;
    }
}

// 调试界面
void MainWindow::on_btn_send_clicked()
{
    QString str = ui->lineEdit_15->text();      // 获取输入值
    QByteArray frame = QByteArray::fromHex(str.remove(' ').toLatin1());     // 转化为字节
    quint16 crc = Modbus_CRC16(reinterpret_cast<uint8_t *>(frame.data()), frame.length());      // CRC校验
    frame.append(static_cast<char>((crc >> 8) & 0xFF)); // CRC高字节
    frame.append(static_cast<char>(crc & 0xFF));        // CRC低字节
    QString strHex = frame.toHex(' ');      // 转化十六进制字符串
    ui->plainTextEdit_2->moveCursor(QTextCursor::End);
    ui->plainTextEdit_2->insertPlainText("【"+QDateTime::currentDateTime().toString("hh:mm:ss:zzz")+"】"+" "+"发："+strHex.toUpper().append(' ')+"\n");//返回发送值到对话框；
    isSetBtn = true;
    sensorSerial.write(frame);      // 串口写入
}

// 十六进制转十进制
void MainWindow::on_lineEdit_42_textEdited(const QString &arg1)
{
    QString hexStr = arg1;
    hexStr.remove(' '); // 去掉空格

    bool ok;
    int value = hexStr.toInt(&ok, 16);  // 按16进制解析
    if (ok) {
        ui->lineEdit_43->setText(QString::number(value, 10));
    } else {
        ui->lineEdit_43->setText("输入错误");
    }
}

// 十进制转十六进制
void MainWindow::on_lineEdit_43_textEdited(const QString &arg1)
{
    QString hexStr = arg1;
    hexStr.remove(' '); // 去掉空格

    bool ok;
    int value = hexStr.toInt(&ok, 10);  // 按10进制解析
    if (ok) {
        ui->lineEdit_42->setText(QString::number(value, 16).toUpper());
    } else {
        ui->lineEdit_42->setText("输入错误");
    }
}

// float转十进制
void MainWindow::on_lineEdit_44_textEdited(const QString &arg1)
{
    QString hexStr = arg1;
    hexStr.remove(' '); // 去掉空格
    QByteArray ba = QByteArray::fromHex(hexStr.toLatin1());

    if (ba.size() == 4) {
        // 按需要调整字节顺序，这里假设大端转小端
        QByteArray swapped;
        swapped.append(ba[3]);
        swapped.append(ba[2]);
        swapped.append(ba[1]);
        swapped.append(ba[0]);

        float value;
        memcpy(&value, swapped.constData(), sizeof(float));

        ui->lineEdit_45->blockSignals(true);
        ui->lineEdit_45->setText(QString::number(value, 'f', 6));
        ui->lineEdit_45->blockSignals(false);
    } else {
        ui->lineEdit_45->setText("输入错误");
    }
}

// 十进制转float
void MainWindow::on_lineEdit_45_textEdited(const QString &arg1)
{
    QString hexStr = arg1;
    hexStr.remove(' '); // 去掉空格

    bool ok;
    float value = hexStr.toFloat(&ok);
    if (ok) {
        QByteArray ba(reinterpret_cast<const char*>(&value), sizeof(float));

        // 按需要调整字节顺序，这里假设小端转大端
        QByteArray swapped;
        swapped.append(ba[3]);
        swapped.append(ba[2]);
        swapped.append(ba[1]);
        swapped.append(ba[0]);

        ui->lineEdit_44->blockSignals(true);
        ui->lineEdit_44->setText(swapped.toHex(' ').toUpper());
        ui->lineEdit_44->blockSignals(false);
    } else {
        ui->lineEdit_44->setText("输入错误");
    }
}

// 清除显示框
void MainWindow::on_pushButton_8_clicked()
{
    ui->plainTextEdit_2->clear();
}
