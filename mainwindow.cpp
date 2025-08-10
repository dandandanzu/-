#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QDebug>
#include <QTime>
#include <QScrollBar>
#include "crc.h"

#define SecondByte_06   0x06
#define LengthByte_00   0x00
#define LengthByte_01   0x01

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    this->setWindowTitle("微雀微压差变送器测试工具V3.0");

    // 刷新全部串口
    serialRefreshInit();

    // 界面初始化
    interfaceInit();

    // 传感器串口接收槽函数
    connect(&sensorSerial, &QSerialPort::readyRead, this, &MainWindow::sensorSerialDelay);
    connect(&sensorSerialDelayTimer, &QTimer::timeout, this, &MainWindow::sensorSerialRead);

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
        ui->plainTextEdit->appendPlainText(QString("写->%1->%2")
                                .arg(address)
                                .arg(value));
            ui->plainTextEdit->moveCursor(QTextCursor::End);
        break;
    case Read:
        ui->plainTextEdit->appendPlainText(QString("读->%1")
                                .arg(address));
            ui->plainTextEdit->moveCursor(QTextCursor::End);
        break;
    case Receive:
        ui->plainTextEdit->appendPlainText(QString("收->%1")
                                               .arg(value));
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
            if (ui->label_25->text() == "uA") {
                strID = 01;
                functionCode = 04;
            } else {
                strID = ui->lineEdit_7->text().toInt();
                functionCode = 05;
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
            if (ui->label_25->text() == "uA") {
                strID = 01;
            } else {
                strID = ui->lineEdit_7->text().toInt();
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
    // 关闭串口接收延时Timer
    if (sensorSerialDelayTimer.isActive()) {
        sensorSerialDelayTimer.stop();
    }

    // 开启定时器接收串口数据延时
    sensorSerialDelayTimer.start(30);
}

// 传感器串口接收
void MainWindow::sensorSerialRead()
{
    // 关闭串口接收延时Timer
    if (sensorSerialDelayTimer.isActive()) {
        sensorSerialDelayTimer.stop();
    }

    // 读取所有可用数据
    QByteArray data = sensorSerial.readAll();
    QString allStr =data.toHex().toUpper();        // 如果数据有误，全部打印出来

    // 检查数据是否有回复
    if (data.size() < 3 && waitingResponseTimer.isActive()) {
        pointerInit();
        ui->plainTextEdit->appendPlainText("！！！返回数据有误！！！");       
        waitingResponseTimer.stop();
        return;
    }

    // 显示在窗口最下边
    ui->statusbar->showMessage("接收->" + data.toHex().toUpper());

    QString valueStr;   // 用来暂存转换后的值，显示在UI界面上    
    // 收到 06 或者 10 显示写入成功，否则是收到 04 代表读取
    if (static_cast<quint8>(data[1]) == 0x06 || static_cast<quint8>(data[1]) == 0x10) {
        appendToTextEdit(Receive, "", "写入成功");
    } else if (static_cast<quint8>(data[1]) == 0x04 || static_cast<quint8>(data[1]) == 0x05) {
        // 1、数据长度为2，2、数据长度为4
        if (static_cast<quint8>(data[2]) == 0x02) {
            // 解析数据
            quint16 value = (static_cast<quint16>(static_cast<quint8>(data[3])) << 8)
                            | static_cast<quint8>(data[4]);
            valueStr = QString::number(value);

            // 调用数据
            if (pressureUnit) {                     // 读取压力单位
                if (static_cast<quint8>(data[4]) == 0x00) {
                    ui->lineEdit_6->setText("Pa");
                    ui->pushButton_108->setStyleSheet("background-color: #00BFFF");
                    ui->pushButton_109->setStyleSheet("");
                    pressureUnit = false;
                } else {
                    ui->lineEdit_6->setText("kPa");
                    ui->pushButton_108->setStyleSheet("");
                    ui->pushButton_109->setStyleSheet("background-color: #00BFFF");
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
            } else if (LineEditID) {                           // 设备ID
                    if (static_cast<quint8>(data[0]) == 0xFF) {         // 连接设备会发起 FF
                        LineEditID->setText(valueStr);
                        appendToTextEdit(Receive, "", "设备连接成功，设备地址是" + valueStr);
                        LineEditID = nullptr;
                    } else {
                        LineEditID->setText(valueStr);
                        appendToTextEdit(Receive, "", LineEditID->text());
                        LineEditID = nullptr;
                    }
            } else if (ComboBoxBaud) {                  // 波特率
                ComboBoxBaud->setCurrentText(baudMap.key(valueStr));
                appendToTextEdit(Receive, "", baudMap.key(valueStr));
                ComboBoxBaud = nullptr;
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
                for (QPushButton *btn : buttonList) {
                    if (btn->text() == sensorListMap.key(valueStr)) {
                        btn->setStyleSheet("background-color: #00BFFF;");
                        break;
                    }
                }
                appendToTextEdit(Receive, "", sensorListMap.key(valueStr));
                isSensor = false;
            } else if (ComboBoxTransmissionMethod) {           // 变送方式列表
                ComboBoxTransmissionMethod->setCurrentText(transmissionMethodListMap.key(valueStr));
                appendToTextEdit(Receive, "", transmissionMethodListMap.key(valueStr));
                ComboBoxTransmissionMethod = nullptr;
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
                isTransmissionMethod = false;
            } else if (LineEditSensorCalibrationNum) {
                LineEditSensorCalibrationNum->setText(valueStr);
                appendToTextEdit(Receive, "", valueStr);
                LineEditSensorCalibrationNum = nullptr;
            }

        } else if (static_cast<quint8>(data[2]) == 0x04) {      // 32位
            // 解析数据
            uint32_t intValue = 0;
            if (data.size() >= 7) {
                QByteArray floatBytes = data.mid(3, 4);             // 从第4字节开始取4字节（C4BB8000）
                for (int i = 0; i < 4; ++i) {
                    intValue |= static_cast<quint8>(floatBytes.at(i)) << (8 * (3 - i));
                }
            } else {
                appendToTextEdit(Receive, "", "接收数据长度有误"+allStr);
                pointerInit();
                waitingResponseTimer.stop();
                return;
            }
            // 如果是float型，否择是32位int型
            if (isFloat) {                                      //  uint32_t 的二进制表示转为 float
                float floatValue;
                memcpy(&floatValue, &intValue, sizeof(float));  // 安全类型转换

                if (std::fabs(floatValue) < 1.0) { // 等价于 value ∈ (-1, 1)
                    floatValue = 0.0;
                }
                valueStr = QString::number(floatValue);         // 转换浮点数为字符串
                isFloat = false;                                // 转换完成后修改状态

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
                    LineEditPressure->setText(valueStr);
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
        }
    } else if (static_cast<quint8>(data[1]) == 0x84) {
        pointerInit();
        appendToTextEdit(Receive, "", "接收到错误码->"+allStr);       
        waitingResponseTimer.stop();
        return;
    }

    waitingResponseTimer.stop();

    // 选择传感器型号时，没有用队列发，此时队列时空的，不能出队
    if (!functionQueue.isEmpty()) {
        functionQueue.dequeue();
    }
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
        if (ui->label_25->text() == "uA"){  // 采用变送起点label辨别当前模式是电压还是电流
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
    // 超时次数为0
    timeoutTimes = 0;
    // 设备ID初始化为FF
    ui->lineEdit_7->setText("255");

    // 0Pa校正不显示
    ui->lineEdit_15->hide();
    ui->pushButton_35->hide();
    ui->pushButton_36->hide();

    // 电流标定的占空比显示
    ui->lineEdit_27->setText("17");
    ui->lineEdit_28->setText("30");
    ui->lineEdit_29->setText("50");
    ui->lineEdit_30->setText("75");
    ui->lineEdit_31->setText("85");

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

    // tabwidget失能
    ui->tabWidget->setEnabled(false);
}

// 延时函数
void MainWindow::delay(int time)
{
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
    ui->pushButton_22->setStyleSheet("background-color: #00BFFF;");
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
    // 按下变绿
//  buttonTrigge(ui->pushButton_3);

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
    // 按下变绿
//  buttonTrigge(ui->pushButton_5);

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
        // 设置你要连接的串口名，例如从 ComboBox 获取
        QString portName = ui->comboBox->currentText();  // 假设你用了 comboBox 选择串口
        sensorSerial.setPortName(portName);

        // 先尝试 open 看是否已被占用
        if (!sensorSerial.open(QIODevice::ReadWrite)) {
            QMessageBox::critical(this, "错误", QString("串口 %1 已被占用或无法打开").arg(portName));
            return;
        }

        // 设置参数（如果 open 成功）
        sensorSerial.setBaudRate(QSerialPort::Baud9600);
        sensorSerial.setDataBits(QSerialPort::Data8);
        sensorSerial.setParity(QSerialPort::NoParity);
        sensorSerial.setStopBits(QSerialPort::OneStop);

        sensorSerialOpen = true;
        ui->pushButton_2->setText("关闭");
        ui->pushButton_2->setStyleSheet("background-color: #00BFFF;");
        ui->comboBox->setEnabled(false);
        ui->pushButton->setEnabled(false);
        ui->tabWidget->setEnabled(true);

        // 清空队列
        delay(10);
        queueClear();
        delay(10);
        if (waitingResponseTimer.isActive()) {
            waitingResponseTimer.stop();
        }

        // 开启自动出队定时器
        autoDequeueTimer.start(60);
    }
    else {
        sensorSerial.close();
        sensorSerialOpen = false;
        ui->pushButton_2->setText("打开");
        ui->pushButton_2->setStyleSheet("");
        ui->pushButton_22->setStyleSheet("");
        ui->comboBox->setEnabled(true);
        ui->pushButton->setEnabled(true);
        ui->tabWidget->setEnabled(false);
        delay(10);
        queueClear();
        delay(10);
        autoDequeueTimer.stop();
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

    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "000E", "C381");
            appendToTextEdit(Write, "恢复出厂设置命令", "恢复出厂设置");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "0010", "01");
            appendToTextEdit(Write, "恢复出厂设置命令", "恢复出厂设置");
        });
    }
}

// 读设备地址
void MainWindow::on_pushButton_12_clicked()
{
    // 插队进入
    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            LineEditID = ui->lineEdit_7;
            serialRead(Portsensor, "0010", "01");
            appendToTextEdit(Read, "设备地址", "");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            LineEditID = ui->lineEdit_7;
            serialRead(Portsensor, "0000", "01");
            appendToTextEdit(Read, "设备地址", "");
        });
    }
}

// 读波特率
void MainWindow::on_pushButton_14_clicked()
{
    // 插队进入
    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0011", "01");
            appendToTextEdit(Read, "波特率", "");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0001", "01");
            appendToTextEdit(Read, "波特率", "");
        });
    }
}

// 读电流下限
void MainWindow::on_pushButton_16_clicked()
{
    // 插队进入
    functionQueue.prepend([this]() {
        LineEditID = ui->lineEdit_9;
        serialRead(Portsensor, "0015", "01");
        appendToTextEdit(Read, "电流/电压下限", "");
    });
}

// 读电流上限
void MainWindow::on_pushButton_18_clicked()
{
    // 插队进入
    functionQueue.prepend([this]() {
        LineEditCurrentMax = ui->lineEdit_10;
        serialRead(Portsensor, "0016", "01");
        appendToTextEdit(Read, "电流/电压上限", "");
    });
}

// 读零点跟踪
void MainWindow::on_pushButton_20_clicked()
{
    // 插队进入
    functionQueue.prepend([this]() {
        LineEditZeroTracking = ui->lineEdit_11;
        serialRead(Portsensor, "001B", "01");
        appendToTextEdit(Read, "零点跟踪", "");
    });
}

// 重启设备
void MainWindow::on_pushButton_36_clicked()
{
    // 防止误操作
   if (QMessageBox::information(this, "警告", "是否要重启设备", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        // 插队进入
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "000E", "E082");

            appendToTextEdit(Write, "重启设备命令", "重启");
        });
    } else {
        return;
    }
}

// 一键读取
void MainWindow::on_pushButton_37_clicked()
{
    autoScanTimer.stop();

    delay(10);
    // 清除队列
    queueClear();
    delay(10);

    ui->pushButton_37->setEnabled(false);

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

    // 读变送方式
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0013", "01");
        isTransmissionMethod = true;
        appendToTextEdit(Read, "读变送方式", "");
    });

    // 当前压力单位
    functionQueue.enqueue([this]() {
        pressureUnit = true;
        serialRead(Portsensor, "0012", "01");
    });

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

    // 设备地址
    if (ui->label_25->text() == "uA") {
        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            serialRead(Portsensor, "0010", "01");
            appendToTextEdit(Read, "设备地址", "");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.enqueue([this]() {
            LineEditID = ui->lineEdit_7;
            serialRead(Portsensor, "0000", "01");
            appendToTextEdit(Read, "设备地址", "");
        });
    }

    // 波特率
    if (ui->label_25->text() == "uA") {
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0011", "01");
            appendToTextEdit(Read, "波特率", "");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.enqueue([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0001", "01");
            appendToTextEdit(Read, "波特率", "");
        });
    }

    // 读电流下限
    functionQueue.enqueue([this]() {
        LineEditID = ui->lineEdit_9;
        serialRead(Portsensor, "0015", "01");
        appendToTextEdit(Read, "电流/电压下限", "");
    });

    // 读电流上限
    functionQueue.enqueue([this]() {
        LineEditCurrentMax = ui->lineEdit_10;
        serialRead(Portsensor, "0016", "01");
        appendToTextEdit(Read, "电流/电压上限", "");
    });

    // 读零点跟踪
    functionQueue.enqueue([this]() {
        LineEditZeroTracking = ui->lineEdit_11;
        serialRead(Portsensor, "001B", "01");
        appendToTextEdit(Read, "零点跟踪", "");
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
        LineEditOutMinPa = ui->lineEdit_17;
        serialRead(Portsensor, "006A", "02");
        appendToTextEdit(Read, "传感器输出最小值（pa）", "");
    });

    // 插队读传感器最大输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMaxPa = ui->lineEdit_18;
        serialRead(Portsensor, "006C", "02");
        appendToTextEdit(Read, "传感器输出最大值（pa）", "");
    });

    // 插队读传感器最小输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMincmH2O = ui->lineEdit_19;
        serialRead(Portsensor, "0066", "02");
        appendToTextEdit(Read, "传感器输出最小值（cmH2O）", "");
    });

    // 插队读传感器最大输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMaxcmH2O = ui->lineEdit_20;
        serialRead(Portsensor, "0068", "02");
        appendToTextEdit(Read, "传感器输出最大值（cmH2O）", "");
    });

    // 插队读传感器最小内码值
    functionQueue.enqueue([this]() {
        isFloat = false;
        LineEditMinInternalCode = ui->lineEdit_21;
        serialRead(Portsensor, "0062", "02");
        appendToTextEdit(Read, "传感器输出最小内码值", "");
    });

    // 插队读传感器满度内码值
    functionQueue.enqueue([this]() {
        LineEditMaxInternalCode = ui->lineEdit_22;
        serialRead(Portsensor, "0064", "02");
        appendToTextEdit(Read, "传感器输出满度内码值", "");
    });

    // 插队读取传感器变送起点uA
    functionQueue.enqueue([this]() {
        LineEditStartPointuA = ui->lineEdit_23;
        serialRead(Portsensor, "001E", "01");
        appendToTextEdit(Read, "传感器变送起点（uA/mV）", "");
    });

    // 插队读取传感器变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditStartPointPa = ui->lineEdit_24;
        serialRead(Portsensor, "0017", "02");
        appendToTextEdit(Read, "传感器变送起点（Pa）", "");
    });

    // 插队读取传感器变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        LineEditEndPointuA = ui->lineEdit_25;
        serialRead(Portsensor, "0020", "01");
        appendToTextEdit(Read, "传感器变送满度（uA/mV）", "");
    });

    // 插队读取传感器变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditEndPointPa = ui->lineEdit_26;
        serialRead(Portsensor, "0019", "02");
        appendToTextEdit(Read, "传感器变送满度（Pa）", "");
    });

    // 读取电流标定点数
    functionQueue.enqueue([this]() {
        isFloat = false;
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 读取电流标定点1电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0029", "01");
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        appendToTextEdit(Read, "电流标定点1", "");
    });

    // 读取电流标定点1对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0042", "02");
        LineEditInternalCode1 = ui->lineEdit_32;
        appendToTextEdit(Read, "电流标定内码1", "");
    });

    // 读取电流标定点2电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002A", "01");
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        appendToTextEdit(Read, "电流标定2", "");
    });

    // 读取电流标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0044", "02");
        LineEditInternalCode2 = ui->lineEdit_33;
        appendToTextEdit(Read, "电流标定内码2", "");
    });

    // 读取电流标定点3电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002B", "01");
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        appendToTextEdit(Read, "电流标定点3", "");
    });

    // 读取电流标定点3对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0046", "02");
        LineEditInternalCode3 = ui->lineEdit_34;
        appendToTextEdit(Read, "电流标定内码3", "");
    });

    // 读取电流标定点4电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002C", "01");
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        appendToTextEdit(Read, "电流标定点4", "");
    });

    // 读取电流标定点4对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0048", "02");
        LineEditInternalCode4 = ui->lineEdit_35;
        appendToTextEdit(Read, "电流标定内码4", "");
    });

    // 读取电流标定点5电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002D", "01");
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        appendToTextEdit(Read, "电流标定5", "");
    });

    // 读取电流标定点5对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "004A", "02");
        LineEditInternalCode5 = ui->lineEdit_36;
        appendToTextEdit(Read, "电流标定内码5", "");
    });

    // 读传感器标定点数
    functionQueue.enqueue([this]() {
        LineEditSensorCalibrationNum = ui->lineEdit_14;
        serialRead(Portsensor, "0050", "01");
        appendToTextEdit(Read, "传感器标定点数", "");
    });

    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0032", "02");
        LineEditSensorCalibration1 = ui->lineEdit_67;
        appendToTextEdit(Read, "传感器标定点1", "");
    });

    // 读传感器标定点1内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0053", "02");
        LineEditSensorInternalCode1 = ui->lineEdit_62;
        appendToTextEdit(Read, "传感器标定点1内码", "");
    });

    // 读传感器标定点2Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0034", "02");
        LineEditSensorCalibration2 = ui->lineEdit_68;

        appendToTextEdit(Read, "传感器标定点2", "");
    });

    // 读传感器标定点2内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0055", "02");
        LineEditSensorInternalCode2 = ui->lineEdit_63;

        appendToTextEdit(Read, "传感器标定点2内码", "");
    });

    // 读传感器标定点3Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0036", "02");
        LineEditSensorCalibration3 = ui->lineEdit_69;
        appendToTextEdit(Read, "传感器标定点3", "");
    });

    // 读传感器标定点3内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0057", "02");
        LineEditSensorInternalCode3 = ui->lineEdit_64;
        appendToTextEdit(Read, "传感器标定点3内码", "");
    });

    // 读传感器标定点4Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0038", "02");
        LineEditSensorCalibration4 = ui->lineEdit_70;

        appendToTextEdit(Read, "传感器标定点4", "");
    });

    // 读传感器标定点4内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0059", "02");
        LineEditSensorInternalCode4 = ui->lineEdit_65;
        appendToTextEdit(Read, "传感器标定点4内码", "");
    });

    // 读传感器标定点5Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "003A", "02");
        LineEditSensorCalibration5 = ui->lineEdit_71;
        appendToTextEdit(Read, "传感器标定点5", "");
    });

    // 读传感器标定点5内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "005B", "02");
        LineEditSensorInternalCode5 = ui->lineEdit_66;
        appendToTextEdit(Read, "传感器标定点5内码", "");
        // 可以读取
        ui->pushButton_37->setEnabled(true);
        // 开启自动扫描定时器
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });
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
        LineEditOutMinPa = ui->lineEdit_17;
        serialRead(Portsensor, "006A", "02");
        appendToTextEdit(Read, "传感器输出最小值（pa）", "");
    });

    // 插队读传感器最大输出值_pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMaxPa = ui->lineEdit_18;
        serialRead(Portsensor, "006C", "02");
        appendToTextEdit(Read, "传感器输出最大值（pa）", "");
    });

    // 插队读传感器最小输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMincmH2O = ui->lineEdit_19;
        serialRead(Portsensor, "0066", "02");
        appendToTextEdit(Read, "传感器输出最小值（cmH2O）", "");
    });

    // 插队读传感器最大输出值_cmH2O
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditOutMaxcmH2O = ui->lineEdit_20;
        serialRead(Portsensor, "0068", "02");
        appendToTextEdit(Read, "传感器输出最大值（cmH2O）", "");
    });

    // 插队读传感器最小内码值
    functionQueue.enqueue([this]() {
        isFloat = false;
        LineEditMinInternalCode = ui->lineEdit_21;
        serialRead(Portsensor, "0062", "02");
        appendToTextEdit(Read, "传感器输出最小内码值", "");
    });

    // 插队读传感器满度内码值
    functionQueue.enqueue([this]() {
        LineEditMaxInternalCode = ui->lineEdit_22;
        serialRead(Portsensor, "0064", "02");
        appendToTextEdit(Read, "传感器输出满度内码值", "");
    });

    // 插队读取传感器变送起点uA
    functionQueue.enqueue([this]() {
        LineEditStartPointuA = ui->lineEdit_23;
        serialRead(Portsensor, "001E", "01");
        appendToTextEdit(Read, "传感器变送起点（uA/mV）", "");
    });

    // 插队读取传感器变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditStartPointPa = ui->lineEdit_24;
        serialRead(Portsensor, "0017", "02");
        appendToTextEdit(Read, "传感器变送起点（Pa）", "");
    });

    // 插队读取传感器变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        LineEditEndPointuA = ui->lineEdit_25;
        serialRead(Portsensor, "0020", "01");
        appendToTextEdit(Read, "传感器变送满度（uA/mV）", "");
    });

    // 插队读取传感器变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        LineEditEndPointPa = ui->lineEdit_26;
        serialRead(Portsensor, "0019", "02");
        appendToTextEdit(Read, "传感器变送满度（Pa）", "");
        // 开启自动扫描定时器
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
        ui->pushButton_33->setEnabled(true);
    });
}

// 写入变送起点和变送满度
void MainWindow::on_pushButton_34_clicked()
{
    // 插队写入变送起点uA
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "001E", hexStr);
        appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
    });

    // 插队写入变送起点Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialWrite(Portsensor, "0017", ui->lineEdit_24->text());
        appendToTextEdit(Write, "变送起点（Pa）", ui->lineEdit_24->text());
    });

    // 插队写入变送满度uA
    functionQueue.enqueue([this]() {
        isFloat = false;
        QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0020", hexStr);
        appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
    });

    // 插队写入变送满度Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialWrite(Portsensor, "0019", ui->lineEdit_26->text());
        appendToTextEdit(Write, "变送满度（Pa）", ui->lineEdit_26->text());
    });
}

// 占空比1设置
void MainWindow::on_pushButton_38_clicked()
{
    // 插队设置占空比
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_27->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比1", ui->lineEdit_27->text());
    });
}

// 占空比2设置
void MainWindow::on_pushButton_39_clicked()
{
    // 插队设置占空比
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_28->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比2", ui->lineEdit_28->text());
    });
}

// 占空比3设置
void MainWindow::on_pushButton_40_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_29->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比3", ui->lineEdit_29->text());
    });
}

// 占空比4设置
void MainWindow::on_pushButton_41_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_30->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比4", ui->lineEdit_30->text());
    });
}

// 占空比5设置
void MainWindow::on_pushButton_42_clicked()
{
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_31->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比5", ui->lineEdit_31->text());
    });
}

// 电流标定点1
void MainWindow::on_pushButton_46_clicked()
{
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_37->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0029", hexStr);
        appendToTextEdit(Write, "电流标定点1", ui->lineEdit_37->text());
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
        serialWrite(Portsensor, "002A", hexStr);
        appendToTextEdit(Write, "电流标定点2", ui->lineEdit_41->text());
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
        serialWrite(Portsensor, "002B", hexStr);
        appendToTextEdit(Write, "电流标定点3", ui->lineEdit_40->text());
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
        serialWrite(Portsensor, "002C", hexStr);
        appendToTextEdit(Write, "电流标定点4", ui->lineEdit_39->text());
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
        serialWrite(Portsensor, "002D", hexStr);
        appendToTextEdit(Write, "电流标定点5", ui->lineEdit_38->text());
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
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "000C", "CD43");

        appendToTextEdit(Write, "清除电流标定点", "清除");
    });
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

    // 读取电流标定点1电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0029", "01");
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        appendToTextEdit(Read, "电流标定点1", "");
    });

    // 读取电流标定点1对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0042", "02");
        LineEditInternalCode1 = ui->lineEdit_32;
        appendToTextEdit(Read, "电流标定内码1", "");
    });

    // 读取电流标定点2电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002A", "01");
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        appendToTextEdit(Read, "电流标定内码5", "");
    });

    // 读取电流标定点2对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0044", "02");
        LineEditInternalCode2 = ui->lineEdit_33;
        appendToTextEdit(Read, "电流标定内码2", "");
    });

    // 读取电流标定点3电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002B", "01");
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        appendToTextEdit(Read, "电流标定点3", "");
    });

    // 读取电流标定点3对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0046", "02");
        LineEditInternalCode3 = ui->lineEdit_34;
        appendToTextEdit(Read, "电流标定内码3", "");
    });

    // 读取电流标定点4电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002C", "01");
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        appendToTextEdit(Read, "电流标定点4", "");
    });

    // 读取电流标定点4对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0048", "02");
        LineEditInternalCode4 = ui->lineEdit_35;
        appendToTextEdit(Read, "电流标定内码4", "");
    });

    // 读取电流标定点5电流uA
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "002D", "01");
        LineEditCurrentCalibration5 = ui->lineEdit_38;
        appendToTextEdit(Read, "电流标定内码5", "");
    });

    // 读取电流标定点5对应PWM定时器计数值
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "004A", "02");
        LineEditInternalCode5 = ui->lineEdit_36;
        appendToTextEdit(Read, "电流标定内码5", "");
        ui->pushButton_49->setEnabled(true);
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });   
}

// 保存电流标定点
void MainWindow::on_pushButton_50_clicked()
{
    // 插队保存
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "000C", "AD53");
        appendToTextEdit(Write, "保存电流标定点", "保存");
    });
}

// 传感器标定点1
void MainWindow::on_pushButton_69_clicked()
{
    // 插队设置传感器标定点1
    functionQueue.prepend([this]() {
        isFloat = true;
        serialWrite(Portsensor, "0032", ui->lineEdit_67->text());
        appendToTextEdit(Write, "传感器标定点1", ui->lineEdit_67->text());
    });

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
    // 按下变绿
//  buttonTrigge(ui->pushButton_70);

    // 插队设置传感器标定点2
    functionQueue.prepend([this]() {
        isFloat = true;
        serialWrite(Portsensor, "0034", ui->lineEdit_68->text());

        appendToTextEdit(Write, "传感器标定点2", ui->lineEdit_68->text());
    });

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
        serialWrite(Portsensor, "0036", ui->lineEdit_69->text());

        appendToTextEdit(Write, "传感器标定点3", ui->lineEdit_69->text());
    });

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
    // 按下变绿
//  buttonTrigge(ui->pushButton_72);

    // 插队设置传感器标定点4
    functionQueue.prepend([this]() {
        isFloat = true;
        serialWrite(Portsensor, "0038", ui->lineEdit_70->text());

        appendToTextEdit(Write, "传感器标定点4", ui->lineEdit_70->text());
    });

    // 标定之后马上读需要延时？

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
    // 按下变绿
//  buttonTrigge(ui->pushButton_73);

    // 插队设置传感器标定点5
    functionQueue.prepend([this]() {
        isFloat = true;
        serialWrite(Portsensor, "003A", ui->lineEdit_71->text());

        appendToTextEdit(Write, "传感器标定点5", ui->lineEdit_71->text());
    });

    // 标定之后马上读需要延时？

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
    // 清除传感器标定
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "000C", "9D63");

        appendToTextEdit(Write, "清除传感器标定点", "清除");
    });
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

    // 读传感器标定点1Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0032", "02");
        LineEditSensorCalibration1 = ui->lineEdit_67;

        appendToTextEdit(Read, "传感器标定点1", "");
    });

    // 读传感器标定点1内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0053", "02");
        LineEditSensorInternalCode1 = ui->lineEdit_62;

        appendToTextEdit(Read, "传感器标定点1内码", "");
    });

    // 读传感器标定点2Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0034", "02");
        LineEditSensorCalibration2 = ui->lineEdit_68;

        appendToTextEdit(Read, "传感器标定点2", "");
    });

    // 读传感器标定点2内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0055", "02");
        LineEditSensorInternalCode2 = ui->lineEdit_63;

        appendToTextEdit(Read, "传感器标定点2内码", "");
    });

    // 读传感器标定点3Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0036", "02");
        LineEditSensorCalibration3 = ui->lineEdit_69;        
        appendToTextEdit(Read, "传感器标定点3", "");
    });

    // 读传感器标定点3内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0057", "02");
        LineEditSensorInternalCode3 = ui->lineEdit_64;        
        appendToTextEdit(Read, "传感器标定点3内码", "");
    });

    // 读传感器标定点4Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "0038", "02");
        LineEditSensorCalibration4 = ui->lineEdit_70;

        appendToTextEdit(Read, "传感器标定点4", "");
    });

    // 读传感器标定点4内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "0059", "02");
        LineEditSensorInternalCode4 = ui->lineEdit_65;
        appendToTextEdit(Read, "传感器标定点4内码", "");
    });

    // 读传感器标定点5Pa
    functionQueue.enqueue([this]() {
        isFloat = true;
        serialRead(Portsensor, "003A", "02");
        LineEditSensorCalibration5 = ui->lineEdit_71;
        appendToTextEdit(Read, "传感器标定点5", "");
    });

    // 读传感器标定点5内码
    functionQueue.enqueue([this]() {
        isFloat = false;
        serialRead(Portsensor, "005B", "02");
        LineEditSensorInternalCode5 = ui->lineEdit_66;
        appendToTextEdit(Read, "传感器标定点5内码", "");
        ui->pushButton_75->setEnabled(true);
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
    });
}

// 保存传感器标定
void MainWindow::on_pushButton_76_clicked()
{
    // 保存传感器标定
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "000C", "8D73");
        appendToTextEdit(Write, "保存传感器标定点", "保存");
    });
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
        return;
    }

    // 发送 FF 04 00 10 00 01 25 D1
    // 第一次连接设备使用FF，下次信息交互采用设备ID
    functionQueue.enqueue([this]() {
        LineEditID = ui->lineEdit_7;
        QByteArray frame;
        frame.append(0xFF);
        frame.append(0x04);
        frame.append(static_cast<char>(LengthByte_00));
        frame.append(0x10)  ;
        frame.append(static_cast<char>(LengthByte_00));
        frame.append(0x01);
        frame.append(0x25);
        frame.append(0xD1);
        sensorSerial.write(frame);

        appendToTextEdit(Read, "开始连接设备", "");
    });

    // 读变送方式
    functionQueue.enqueue([this]() {
        serialRead(Portsensor, "0013", "01");
        isTransmissionMethod = true;
        appendToTextEdit(Read, "读变送模式", "");
    });

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

    // 开启自动扫描
    if (ui->lineEdit_8->text().isEmpty()) {
        QMessageBox::information(this, "错误", "间隔（ms）不能为空", QMessageBox::Ok);
        return;
    } else {
        autoScanTimer.start(ui->lineEdit_8->text().toInt());
        isAutoScan = true;
        ui->pushButton_22->setStyleSheet("background-color: #00BFFF;");
    }
}

// 写设备地址
void MainWindow::on_pushButton_13_clicked()
{
    // 入队
    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_7->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0010", hexStr);
            appendToTextEdit(Write, "设备地址", ui->lineEdit_7->text());
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_7->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0000", hexStr);
            appendToTextEdit(Write, "设备地址", ui->lineEdit_7->text());
        });
    }
}

// 写波特率
void MainWindow::on_pushButton_15_clicked()
{
    // 入队
    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "0011", baudMap.value(ui->comboBox_6->currentText()).rightJustified(4, '0'));
            appendToTextEdit(Write, "波特率", ui->comboBox_6->currentText());
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "0001", baudMap.value(ui->comboBox_6->currentText()).rightJustified(4, '0'));
            appendToTextEdit(Write, "波特率", ui->comboBox_6->currentText());
        });
    }
}

// 写电流下限
void MainWindow::on_pushButton_17_clicked()
{
    // 入队
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_9->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0015", hexStr);
        appendToTextEdit(Write, "电流/电压下限", ui->lineEdit_9->text());
    });
}

// 写电流上限
void MainWindow::on_pushButton_19_clicked()
{
    // 入队
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0016", hexStr);
        appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
    });
}

// 写零点跟踪
void MainWindow::on_pushButton_21_clicked()
{
    // 入队
    functionQueue.prepend([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_11->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "001B", hexStr);
        appendToTextEdit(Write, "零点跟踪", ui->lineEdit_11->text());
    });
}

// 0Pa校正
void MainWindow::on_pushButton_10_clicked()
{
    // 设置OPA校正
    if (ui->label_25->text() == "uA") {
        functionQueue.prepend([this]() {
            serialWrite(Portsensor, "000C", "0030");
            appendToTextEdit(Write, "0pA校正", "校正");
        });
    } else if (ui->label_25->text() == "mV") {
        functionQueue.prepend([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_15->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0000", hexStr);
            appendToTextEdit(Read, "校正", "");
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
    if (ui->label_25->text() == "uA") {
        functionQueue.enqueue([this]() {
            serialWrite(Portsensor, "000C", "4253");
            appendToTextEdit(Write, "变送模式", "变送模式使能");
        });
    }   else if (ui->label_25->text() == "mV") {
        functionQueue.enqueue([this]() {
            serialWrite(Portsensor, "000F", "00");
            appendToTextEdit(Write, "变送模式", "变送模式使能");
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
    LineEditPressure = nullptr;
    LineEditInternalCode = nullptr;
    LineEditCurrent = nullptr;
    LineEditPWM = nullptr;
    LineEditTemprature = nullptr;

    // ComboBox
    ComboBoxBaud = nullptr;
    ComboBoxTransmissionMethod = nullptr;
}

// 自动出队槽函数
void MainWindow::autoDequeueSolt()
{
    if (functionQueue.isEmpty()) return;
    waitingResponseTimer.start(50);      // 主要针对自动扫描
    auto func = functionQueue.head();    // 出队
    func();
}

// 等待回复超时时间
void MainWindow::waitingResponseTimerSolt()
{
    // 自动扫描停止
    autoScanTimer.stop();
    ui->pushButton_22->setStyleSheet("");

    // 窗口提示
    appendToTextEdit(Receive, "", "接收数据超时");
    waitingResponseTimer.stop();
    if (timeoutTimes < 10) {
        timeoutTimes++;
    } else {
        delay(10);
        queueClear();
        delay(10);
        timeoutTimes = 0;
    }
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
    if (ui->label_25->text() == "uA") {
        delay(1000);
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "电流测量", "");
    }

    // 占空比1
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_27->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比1", ui->lineEdit_27->text());
    });

    // 修改占空比，填充电流
    if (ui->label_25->text() == "uA") {
        delay(1500);
        LineEditCurrentCalibration1 = ui->lineEdit_37;
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_27->text()+"% 时电流", "");
    }

    delay(1000);
    functionQueue.enqueue([this]() {

        QString hexStr = QString("%1").arg(ui->lineEdit_37->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0029", hexStr);
        appendToTextEdit(Write, "电流标定点1", ui->lineEdit_37->text());
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 占空比2
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_28->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比2", ui->lineEdit_28->text());
    });

    if (ui->label_25->text() == "uA") {
        delay(1500);
        LineEditCurrentCalibration2 = ui->lineEdit_41;
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_28->text()+"% 时电流", "");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_41->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "002A", hexStr);
        appendToTextEdit(Write, "电流标定点2", ui->lineEdit_41->text());
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 占空比3
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_29->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比3", ui->lineEdit_29->text());
    });

    if (ui->label_25->text() == "uA") {
        delay(1500);
        LineEditCurrentCalibration3 = ui->lineEdit_40;
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_29->text()+"% 时电流", "");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_40->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "002B", hexStr);
        appendToTextEdit(Write, "电流标定点3", ui->lineEdit_40->text());
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 占空比4
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_30->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比4", ui->lineEdit_30->text());
    });

    if (ui->label_25->text() == "uA") {
        delay(1500);
        LineEditCurrentCalibration4 = ui->lineEdit_39;
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_30->text()+"% 时电流", "");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_39->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "002C", hexStr);
        appendToTextEdit(Write, "电流标定点4", ui->lineEdit_39->text());
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
    });

    // 占空比5
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_31->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0028", hexStr);
        appendToTextEdit(Write, "占空比5", ui->lineEdit_31->text());
    });

    if (ui->label_25->text() == "uA") {
        delay(1500);
        LineEditCurrentCalibration1 = ui->lineEdit_38;
        serialWrite(PortMultimeter, "", ":MEASure:CURRent:DC?");
        appendToTextEdit(Read, "读占空比"+ui->lineEdit_31->text()+"% 时电流", "");
    }

    delay(1000);
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_38->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "002D", hexStr);
        appendToTextEdit(Write, "电流标定点5", ui->lineEdit_38->text());
    });

    functionQueue.enqueue([this]() {
        LineEditCurrentCalibrationNum = ui->lineEdit_13;
        serialRead(Portsensor, "0041", "01");
        appendToTextEdit(Read, "电流标定点数", "");
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
        btn->setStyleSheet("background-color: #006633;");
    }
    button->setStyleSheet("background-color: #00BFFF;");
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
    appendToTextEdit(Write, "传感器型号", button->text());
    delay(200);
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
    // 入队
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "0012", "00");
        appendToTextEdit(Write, "压力单位", "Pa");
    });

    functionQueue.enqueue([this]() {
        pressureUnit = true;
        serialRead(Portsensor, "0012", "01");
        appendToTextEdit(Read, "压力单位", "");
    });
}

// 写入kPa
void MainWindow::on_pushButton_109_clicked()
{
    // 入队
    functionQueue.enqueue([this]() {
        serialWrite(Portsensor, "0012", "01");
        appendToTextEdit(Write, "压力单位", "kPa");
    });

    functionQueue.enqueue([this]() {
        pressureUnit = true;
        serialRead(Portsensor, "0012", "01");
        appendToTextEdit(Read, "压力单位", "");
    });
}

// 设置变送方式
void MainWindow::setTransmissionMethod(QPushButton *button)
{
    // 关闭自动扫描定时器
    if (autoScanTimer.isActive()) {
        autoScanTimer.stop();
    }
    // 先把三个按钮全换成墨绿色
    ui->pushButton_26->setStyleSheet("background-color: #006633;");
    ui->pushButton_28->setStyleSheet("background-color: #006633;");
    ui->pushButton_29->setStyleSheet("background-color: #006633;");

    // 变送方式
    if (button->text() == "4mA~20mA") {
        functionQueue.enqueue([this]() {
            ui->pushButton_26->setStyleSheet("background-color: #00BFFF;");
            serialWrite(Portsensor, "0013", "00");
            appendToTextEdit(Write, "变送方式", "4mA~20mA");
        });

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "001E", hexStr);
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0020", hexStr);
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
        });

        // 读波特率
        functionQueue.prepend([this]() {
            ComboBoxBaud = ui->comboBox_6;
            serialRead(Portsensor, "0011", "01");
            appendToTextEdit(Read, "波特率", "");
        });

    } else if (button->text() == "0V~5V") {
        functionQueue.enqueue([this]() {
            ui->pushButton_28->setStyleSheet("background-color: #00BFFF;");
            serialWrite(Portsensor, "0013", "01");
            appendToTextEdit(Write, "变送方式", "0V~5V");
        });

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "001E", hexStr);
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0020", hexStr);
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
        });
    } else if (button->text() == "0V~10V") {
        functionQueue.enqueue([this]() {
            ui->pushButton_29->setStyleSheet("background-color: #00BFFF;");
            serialWrite(Portsensor, "0013", "02");
            appendToTextEdit(Write, "变送方式", "0V~10V");
        });

        // 插队写入变送起点uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "001E", hexStr);
            appendToTextEdit(Write, "变送起点（uA/mV）", ui->lineEdit_23->text());
        });

        // 插队写入变送满度uA
        functionQueue.enqueue([this]() {
            QString hexStr = QString("%1").arg(ui->lineEdit_25->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
            serialWrite(Portsensor, "0020", hexStr);
            appendToTextEdit(Write, "变送满度（uA/mV）", ui->lineEdit_25->text());
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
        serialWrite(Portsensor, "0020", hexStr);
        appendToTextEdit(Write, "满度信号", ui->lineEdit_25->text());
    });

    // 设置起点信号
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_23->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "001E", hexStr);
        appendToTextEdit(Write, "传感器变送起点（uA/mV）", ui->lineEdit_23->text());
    });

    // 设置电流输出最大值
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_10->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0016", hexStr);
        appendToTextEdit(Write, "电流/电压上限", ui->lineEdit_10->text());
    });

    // 设置电流输出最小值
    functionQueue.enqueue([this]() {
        QString hexStr = QString("%1").arg(ui->lineEdit_9->text().toInt(), 2, 16, QLatin1Char('0')).toUpper();
        serialWrite(Portsensor, "0015", hexStr);
        appendToTextEdit(Write, "电流/电压下限", ui->lineEdit_9->text());
    });

    // 重新打开自动扫描定时器
    autoScanTimer.start(ui->lineEdit_8->text().toInt());
}

// 当前状态
void MainWindow::currentTransmissionMethod(const QString &arg1)
{
    if (arg1 == "0") {         // 4mA~20mA
        // 参数配置修改
        ui->label_9 ->setText("电流下限(uA)");
        ui->label_10->setText("电流上限(uA)");
        ui->lineEdit_9 ->setText("3800");
        ui->lineEdit_10->setText("20200");

        // 传感器配置修改
        ui->label_25->setText("uA");
        ui->label_28->setText("uA");
        ui->lineEdit_23->setText("4000");
        ui->lineEdit_25->setText("20000");

        // 电流标定修改
        ui->label_31->setText("           0~100   内码   uA");
        ui->lineEdit_27->setText("17");
        ui->lineEdit_28->setText("30");
        ui->lineEdit_29->setText("50");
        ui->lineEdit_30->setText("75");
        ui->lineEdit_31->setText("85");
        ui->pushButton_26->setStyleSheet("background-color: #00BFFF;");
        appendToTextEdit(Receive, "", "4mA~20mA");
    } else if (arg1 == "1") {   // 0V~5V
        // 参数配置修改
        ui->label_9 ->setText("电压下限(mV)");
        ui->label_10->setText("电压上限(mV)");
        ui->lineEdit_9 ->setText("0");
        ui->lineEdit_10->setText("6000");

        // 传感器配置修改
        ui->label_25->setText("mV");
        ui->label_28->setText("mV");
        ui->lineEdit_23->setText("0");
        ui->lineEdit_25->setText("5000");

        // 电流标定修改
        ui->label_31->setText("          0~100  内码    mV");
        ui->lineEdit_27->setText("5");
        ui->lineEdit_28->setText("15");
        ui->lineEdit_29->setText("35");
        ui->lineEdit_30->setText("50");
        ui->lineEdit_31->setText("70");
        ui->pushButton_28->setStyleSheet("background-color: #00BFFF;");
        appendToTextEdit(Receive, "", "0V~5V");
    } else if (arg1 == "2") {  // 0V~10V
        // 参数配置修改
        ui->label_9 ->setText("电压下限(mV)");
        ui->label_10->setText("电压上限(mV)");
        ui->lineEdit_9 ->setText("0");
        ui->lineEdit_10->setText("11000");

        // 传感器配置修改
        ui->label_25->setText("mV");
        ui->label_28->setText("mV");
        ui->lineEdit_23->setText("0");
        ui->lineEdit_25->setText("10000");

        // 电流标定修改
        ui->label_31->setText("          0~100  内码    mV");
        ui->lineEdit_27->setText("5");
        ui->lineEdit_28->setText("15");
        ui->lineEdit_29->setText("35");
        ui->lineEdit_30->setText("50");
        ui->lineEdit_31->setText("70");
        ui->pushButton_29->setStyleSheet("background-color: #00BFFF;");
        appendToTextEdit(Receive, "", "0V~10V");
    }
}

void MainWindow::queueClear()
{
    functionQueue.clear();
    pointerInit();
    ui->pushButton_37->setEnabled(true);
    ui->pushButton_33->setEnabled(true);
    ui->pushButton_49->setEnabled(true);
    ui->pushButton_75->setEnabled(true);
}

// 选择变送方式为4mA~20mA
void MainWindow::on_pushButton_26_clicked()
{
    // 隐藏校正数据
    ui->lineEdit_15->hide();
    ui->pushButton_35->hide();
    ui->pushButton_36->hide();

    ui->label_9 ->setText("电流下限(uA)");
    ui->label_10->setText("电流上限(uA)");
    ui->lineEdit_9 ->setText("3800");
    ui->lineEdit_10->setText("20200");

    // 传感器配置修改
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
    ui->pushButton_26->setStyleSheet("background-color: #00BFFF;");
    setTransmissionMethod(ui->pushButton_26);
}

// 选择变送方式为0V~5V
void MainWindow::on_pushButton_28_clicked()
{
    // 参数配置修改
    ui->lineEdit_15->show();
    ui->pushButton_35->show();
    ui->pushButton_36->show();

    ui->label_9 ->setText("电压下限(mV)");
    ui->label_10->setText("电压上限(mV)");
    ui->lineEdit_9 ->setText("0");
    ui->lineEdit_10->setText("6000");

    // 传感器配置修改
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
    ui->label_31->setText("          0~100  内码    mV");
    ui->lineEdit_27->setText("5");
    ui->lineEdit_28->setText("15");
    ui->lineEdit_29->setText("35");
    ui->lineEdit_30->setText("50");
    ui->lineEdit_31->setText("70");
    ui->pushButton_28->setStyleSheet("background-color: #00BFFF;");
    setTransmissionMethod(ui->pushButton_28);
}

// 选择变送方式为0V~10V
void MainWindow::on_pushButton_29_clicked()
{
    ui->lineEdit_15->show();
    ui->pushButton_35->show();
    ui->pushButton_36->show();

    ui->label_9 ->setText("电压下限(mV)");
    ui->label_10->setText("电压上限(mV)");
    ui->lineEdit_9 ->setText("0");
    ui->lineEdit_10->setText("11000");

    // 传感器配置修改
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
    ui->label_31->setText("          0~100  内码    mV");
    ui->lineEdit_27->setText("5");
    ui->lineEdit_28->setText("15");
    ui->lineEdit_29->setText("35");
    ui->lineEdit_30->setText("50");
    ui->lineEdit_31->setText("70");
    ui->pushButton_29->setStyleSheet("background-color: #00BFFF;");
    setTransmissionMethod(ui->pushButton_29);
}

