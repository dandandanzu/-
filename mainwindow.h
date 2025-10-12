#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QPushButton>
#include <QTimer>
#include <QMap>
#include <QComboBox>
#include <QEventLoop>
#include <QQueue>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    enum SerialPortID {
        Portsensor = 0,
        PortMultimeter = 1,
        PortBarograph = 2
    };

    enum SerialPortState {
        Write     = 0,
        Read      = 1,
        Receive   = 2,
    };

    QSerialPort sensorSerial, MultimeterSerial, BarographSerial;
    bool sensorSerialOpen     = false;  // 串口状态标志
    bool MultimeterSerialOpen = false;
    bool BarographSerialOpen  = false;
    bool pressureUnit         = false;
    bool m_autoScrollEnabled  = false;

    bool isTemperature        = false;
    bool isFirmwareVersion    = false;
    bool isFirmwareinfo       = false;
    bool isAutoScan           = false;
    bool isSensor             = false;
    bool isTransmissionMethod = false;

    bool isFloat              = false;
    bool isVoltageSensor      = false;
    bool isDeviceID           = false;
    bool setpressureUnit      = false;
    bool isSetBtn             = false;

    int timeoutTimes, errValueTimes;

    QList<QPushButton*> buttonList, currCaliButtonList, sensCaliButtonList;

    struct RegisterInfo {
        QString name;
        QString address;
        QString count;
        QString type;
        QString value;
        QString description;
    };

    QMap<QString, QString> sensorListMap;
    QMap<QString, QString> voltageSensorListMap;
    QMap<QString, QString> transmissionMethodListMap;
    QMap<QString, QString> baudMap;
    QMap<QString, QString> voltageBaudMap;

    // 参数配置
    QLineEdit *LineEditID           = nullptr,
        *LineEditCurrentMin         = nullptr,
        *LineEditCurrentMax         = nullptr,
        *LineEditZeroTracking       = nullptr,

        // 传感器配置
        *LineEditOutMinPa           = nullptr,
        *LineEditOutMaxPa           = nullptr,
        *LineEditOutMincmH2O        = nullptr,
        *LineEditOutMaxcmH2O        = nullptr,
        *LineEditMinInternalCode    = nullptr,
        *LineEditMaxInternalCode    = nullptr,
        *LineEditStartPointuA       = nullptr,
        *LineEditStartPointPa       = nullptr,
        *LineEditEndPointuA         = nullptr,
        *LineEditEndPointPa         = nullptr,

        // 电流标定
        *LineEditDutyCycle1         = nullptr,
        *LineEditDutyCycle2         = nullptr,
        *LineEditDutyCycle3         = nullptr,
        *LineEditDutyCycle4         = nullptr,
        *LineEditDutyCycle5         = nullptr,
        *LineEditInternalCode1      = nullptr,
        *LineEditInternalCode2      = nullptr,
        *LineEditInternalCode3      = nullptr,
        *LineEditInternalCode4      = nullptr,
        *LineEditInternalCode5      = nullptr,
        *LineEditCurrentCalibration1    = nullptr,
        *LineEditCurrentCalibration2    = nullptr,
        *LineEditCurrentCalibration3    = nullptr,
        *LineEditCurrentCalibration4    = nullptr,
        *LineEditCurrentCalibration5    = nullptr,
        *LineEditCurrentCalibrationNum  = nullptr,

        // 传感器标定
        *LineEditSensorCalibration1     = nullptr,
        *LineEditSensorCalibration2     = nullptr,
        *LineEditSensorCalibration3     = nullptr,
        *LineEditSensorCalibration4     = nullptr,
        *LineEditSensorCalibration5     = nullptr,
        *LineEditSensorInternalCode1    = nullptr,
        *LineEditSensorInternalCode2    = nullptr,
        *LineEditSensorInternalCode3    = nullptr,
        *LineEditSensorInternalCode4    = nullptr,
        *LineEditSensorInternalCode5    = nullptr,
        *LineEditSensorCalibrationNum   = nullptr,

        // 自动扫描配置
        *LineEditPressure           = nullptr,
        *LineEditInternalCode       = nullptr,
        *LineEditCurrent            = nullptr,
        *LineEditPWM                = nullptr,
        *LineEditTemprature         = nullptr,

        // 电压传感器特有
        *LineEditPoint              = nullptr;

    QComboBox *ComboBoxBaud         = nullptr,
        *ComboBoxTransmissionMethod = nullptr,
        *ComboBoxCheck              = nullptr;

    QTimer sensorSerialDelayTimer;

    QTimer MultimeterSerialDelayTimer;

    QTimer autoScanTimer;

    QTimer autoDequeueTimer;

    QTimer waitingResponseTimer;

    QQueue<std::function<void()>> functionQueue;

    QByteArray data;        // 全局缓冲数据

    QString receDeviceID;       // 写设备地址会冲突，此值为读到的值

    int decimalPoint;

    void serialRefreshInit();

    void appendToTextEdit(SerialPortState state, const QString &address, const QString &value);

    void serialRead(SerialPortID id, QString address, QString length);

    void serialWrite(SerialPortID id, QString address, QString data);

    void sensorSerialDelay();

    void multimeterSerialDelay();

    void multimeterSerialRead();

    void barographSerialRead();

    void interfaceInit();

    void regInit();

    void pushButtonInit();

    void delay(int time);

    void autoScan();

    void closeEvent(QCloseEvent *event);

    void selectSensor(QPushButton *button);

    void setTransmissionMethod(QPushButton *button);

    void currentTransmissionMethod(const QString &arg1);

    void queueClear();

    void pushButtonDisEnable(QPushButton *button);

    void pushButtonEnable(QPushButton *button);

private slots:
    void on_pushButton_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_5_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_4_clicked();

    void on_pushButton_6_clicked();

    void on_pushButton_7_clicked();

    void on_pushButton_35_clicked();

    void on_pushButton_12_clicked();

    void on_pushButton_14_clicked();

    void on_pushButton_16_clicked();

    void on_pushButton_18_clicked();

    void on_pushButton_20_clicked();

    void on_pushButton_36_clicked();

    void on_pushButton_37_clicked();

    void on_pushButton_33_clicked();

    void on_pushButton_34_clicked();

    void on_pushButton_38_clicked();

    void on_pushButton_39_clicked();

    void on_pushButton_40_clicked();

    void on_pushButton_41_clicked();

    void on_pushButton_42_clicked();

    void on_pushButton_46_clicked();

    void on_pushButton_45_clicked();

    void on_pushButton_44_clicked();

    void on_pushButton_47_clicked();

    void on_pushButton_43_clicked();

    void on_pushButton_48_clicked();

    void on_pushButton_49_clicked();

    void on_pushButton_50_clicked();

    void on_pushButton_69_clicked();

    void on_pushButton_70_clicked();

    void on_pushButton_71_clicked();

    void on_pushButton_72_clicked();

    void on_pushButton_73_clicked();

    void on_pushButton_74_clicked();

    void on_pushButton_75_clicked();

    void on_pushButton_76_clicked();

    void on_pushButton_22_clicked();

    void on_pushButton_13_clicked();

    void on_pushButton_15_clicked();

    void on_pushButton_17_clicked();

    void on_pushButton_19_clicked();

    void on_pushButton_21_clicked();

    void on_pushButton_10_clicked();

    void on_pushButton_11_clicked();

    void pointerInit();

    void autoDequeueSolt();

    void waitingResponseTimerSolt();

    void on_pushButton_25_clicked();

    void on_pushButton_27_clicked();

    void on_pushButton_30_clicked();

    void on_pushButton_31_clicked();

    void on_pushButton_51_clicked();

    void on_pushButton_52_clicked();

    void on_pushButton_53_clicked();

    void on_pushButton_102_clicked();

    void on_pushButton_106_clicked();

    void on_pushButton_105_clicked();

    void on_pushButton_104_clicked();

    void on_pushButton_103_clicked();

    void on_pushButton_108_clicked();

    void on_pushButton_109_clicked();

    void on_pushButton_26_clicked();

    void on_pushButton_28_clicked();

    void on_pushButton_29_clicked();

    void on_pushButton_23_clicked();

    void on_pushButton_24_clicked();

    void on_pushButton_32_clicked();

    void on_pushButton_54_clicked();

    void on_btn_send_clicked();

    void on_lineEdit_42_textEdited(const QString &arg1);

    void on_lineEdit_43_textEdited(const QString &arg1);

    void on_lineEdit_44_textEdited(const QString &arg1);

    void on_lineEdit_45_textEdited(const QString &arg1);

    void on_pushButton_8_clicked();

private:

    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
