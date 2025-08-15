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

    int timeoutTimes, errValueTimes;

    struct RegisterInfo {
        QString name;
        QString address;
        QString count;
        QString type;
        QString value;
        QString description;
    };

    QList<QPushButton*> buttonList;
    // QList<RegisterInfo> registerList = {
    //     {"FW_VER", "40000", "1","uint16","0","固件版本"},
    //     {"TIMESTAMP" ,"40001", "2","uint32","0","固件信息"},
    //     {"REG_1I_TEMPATURE", "40003", "1", "uint16", "0", "当前温度，个位是小数位。比如寄存器的值为256，实际温度为25.6℃"},
    //     {"REG_2F_PRESSURE_Pa", "40004", "2", "float", "0", "当前压力，单位pa"},
    //     {"REG_2F_PRESSURE_UNIT", "40006", "2", "float", "0", "当前压力，单位由压力单位寄存器决定"},
    //     {"REG_1I_SEN_INNERCODE", "40009", "1", "uint16", "0", "传感器内码"},
    //     {"REG_1I_I_OUTPUT_uA", "40008", "1", "uint16", "0", "当前电流,单位uA"},
    //     {"REG_1U_I_PWM", "40010", "1", "uint16", "0", "电流输出对应的PWM"},
    //     {"REG_1U_I_STATE", "40011", "1", "uint16", "0", "状态寄存器"},
    //     {"REG_1U_CALIB_DATA_CMD", "40012", "1", "uint16", "0", "0x30:置零；0xAD53:保存电流标定参数；0x8D73:保存传感器标定参数；0x9D63:清除传感器标定参数；0xCD43:清除电流标定参数；0x4253:退出其他模式，进入变送模式"},
    //     {"REG_1U_SYS_CMD", "40014", "1", "uint16", "0", "系统命令：0x52AD 重启，0x46B9 清除某模式"},
    //     {"REG_1U_TRANS_CMD" ,"40015",  "1","uint16","0","变送命令"},
    //     {"DEVICE_ID", "40016", "1", "uint16", "0", "设备站号"},
    //     {"REG_1U_COM_BAUDE", "40017", "1", "uint16", "0", "波特率"},
    //     {"REG_1U_SEN_UNIT", "40018", "1", "uint16", "0", "压力单位"},
    //     {"REG_1U_TRANS_MODE", "40019", "1", "uint16", "0", "变送模式，0,4~20mA    1,0V~5V     2,0V~10V       3,0~20mA"},
    //     {"REG_2F_TRANS_STAR_Pa", "40023", "2", "float", "0", "变送器变送零点气压，单位pa"},
    //     {"REG_2F_TRANS_FSC_Pa", "40025", "2", "float", "0", "变送器变送满度气压，单位pa"},
    //     {"REG_1U_ZERO_RANGE_01Pa", "40027", "1", "uint16", "0", "零点归零范围设置，在此范围内，变送输出电流均为零点电流(单位0.1Pa）"},
    //     {"REG_1U_VI_OUTPUT_MIN_uA", "40021", "1", "uint16", "0", "电流输出最小值"},
    //     {"REG_1U_VI_OUTPUT_MAX_uA", "40022", "1", "uint16", "0", "电流输出最大值"},
    //     {"REG_1U_VI_Trans_Start_uAmV", "40030", "1", "uint16", "0", "变送输出起点信号"},
    //     {"REG_1U_VI_Trans_FSC_uAmV", "40032", "1", "uint16", "0", "变送输出满度信号"},
    //     {"REG_1U_BACK_LIGHT_SET", "40033", "1", "uint16", "0", "背光工作模式设置"},
    //     {"REG_1U_SensorType", "40097", "1", "uint16", "0", "传感器型号"},
    //     {"REG_2I_SensorMin_code", "40098", "2", "int32", "0", "数字传感器最小内码值"},
    //     {"REG_2I_SensorMax_code", "40100", "2", "int32", "0", "数字传感器满度内码值"},
    //     {"REG_2F_SensorMinVal_cmH2O", "40102", "2", "float", "0", "传感器最小输出值，零点值  (单位:cmH2O)"},
    //     {"REG_2F_SensorMaxVal_cmH2O", "40104", "2", "float", "0", "传感器最大输出值，满量程值 (单位:cmH2O)"},
    //     {"REG_2F_SensorMinVal_Pa", "40106", "2", "float", "0", "传感器最小输出值，零点值  (单位:Pa)"},
    //     {"REG_2F_SensorMaxVal_Pa", "40108", "2", "float", "0", "传感器最大输出值，零点值  (单位:Pa)"},
    //     {"REG_1U_I_Calib_flg", "40064", "1", "uint16", "0", "电流校准标志， I_CALIBRATE_FLG"},
    //     {"REG_1U_I_CalibPointNum", "40065", "1", "uint16", "0", "电流标定点数"},
    //     {"REG_1U_PWM", "40040", "1", "uint16", "0", "设置PWM占空比,输入参数0~100"},
    //     {"REG_1U_I_Calib1_CurVal", "40041", "1", "uint16", "0", "电流输出校准点1电流"},
    //     {"REG_1U_I_Calib2_CurVal", "40042", "1", "uint16", "0", "电流输出校准点2电流"},
    //     {"REG_1U_I_Calib3_CurVal", "40043", "1", "uint16", "0", "电流输出校准点3电流"},
    //     {"REG_1U_I_Calib4_CurVal", "40044", "1", "uint16", "0", "电流输出校准点4电流"},
    //     {"REG_1U_I_Calib5_CurVal", "40045", "1", "uint16", "0", "电流输出校准点5电流"},
    //     {"REG_2U_I_Calib1_PWM_Val", "40066", "2", "uint32", "0", "电流输出校准点1对应的PWM定时器计数值"},
    //     {"REG_2U_I_Calib2_PWM_Val", "40068", "2", "uint32", "0", "电流输出校准点2对应的PWM定时器计数值"},
    //     {"REG_2U_I_Calib3_PWM_Val", "40070", "2", "uint32", "0", "电流输出校准点3对应的PWM定时器计数值"},
    //     {"REG_2U_I_Calib4_PWM_Val", "40072", "2", "uint32", "0", "电流输出校准点4对应的PWM定时器计数值"},
    //     {"REG_2U_I_Calib5_PWM_Val", "40074", "2", "uint32", "0", "电流输出校准点5对应的PWM定时器计数值"},
    //     {"REG_1U_sensorCalib_flg", "40079", "1", "uint16", "0", ""},
    //     {"REG_1U_sensorCalibPointNum", "40080", "1", "uint16", "0", "传感器标定点数量"},
    //     {"REG_2F_sensor_Calib1_Pa", "40050", "2", "float", "0", "传感器标定1"},
    //     {"REG_2F_sensor_Calib2_Pa", "40052", "2", "float", "0", "传感器标定2"},
    //     {"REG_2F_sensor_Calib3_Pa", "40054", "2", "float", "0", "传感器标定3"},
    //     {"REG_2F_sensor_Calib4_Pa", "40056", "2", "float", "0", "传感器标定4"},
    //     {"REG_2F_sensor_Calib5_Pa", "40058", "2", "float", "0", "传感器标定5"},
    //     {"REG_2I_sensor_0Pa_innercode", "40081", "2", "int32", "0", "传感器变送两点偏移量"},
    //     {"REG_2I_sensor_1Pa_innercode", "40083", "2", "int32", "0", ""},
    //     {"REG_2I_sensor_2Pa_innercode", "40085", "2", "int32", "0", ""},
    //     {"REG_2I_sensor_3Pa_innercode", "40087", "2", "int32", "0", ""},
    //     {"REG_2I_sensor_4Pa_innercode", "40089", "2", "int32", "0", ""},
    //     {"REG_2I_sensor_5Pa_innercode", "40091", "2", "int32", "0", ""},
    //     {"REG_2I_TransZero_InnerCode", "40093", "1", "uint16", "0", ""},
    //     {"REG_2I_TransFSC_InnerCode", "40095", "1", "uint16", "0", ""},
    //     {"REG_1U_init_flg1", "40200", "1", "uint16", "0", "初始化标志"},
    //     {"REG_1U_init_flg2", "40201", "1", "uint16", "0", "数据结构校验和"},
    //     };

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
        *LineEditPoint              = nullptr,
        *LineEditCheck              = nullptr;

    QComboBox *ComboBoxBaud         = nullptr,
        *ComboBoxTransmissionMethod = nullptr;

    QTimer sensorSerialDelayTimer;

    QTimer MultimeterSerialDelayTimer;

    QTimer autoScanTimer;

    QTimer autoDequeueTimer;

    QTimer waitingResponseTimer;

    QEventLoop loop;

    QQueue<std::function<void()>> functionQueue;

    void serialRefreshInit();

    void appendToTextEdit(SerialPortState state, const QString &address, const QString &value);

    void serialRead(SerialPortID id, QString address, QString length);

    void serialWrite(SerialPortID id, QString address, QString data);

    void sensorSerialDelay();

    void sensorSerialRead();

    void multimeterSerialDelay();

    void multimeterSerialRead();

    void barographSerialRead();

    void interfaceInit();

    void delay(int time);

    void autoScan();

    void closeEvent(QCloseEvent *event);

    void selectSensor(QPushButton *button);

    void setTransmissionMethod(QPushButton *button);

    void currentTransmissionMethod(const QString &arg1);

    void queueClear();

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

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
