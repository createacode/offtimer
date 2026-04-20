#ifndef MAIN_H
#define MAIN_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QDateTime>
#include <QSettings>
#include <QSharedMemory>
#include <QFile>
#include <QTextStream>
#include <QGroupBox>
#include <QMessageBox>
#include <QPointer>
#include <QDir>

// 自定义两位数显示的 QSpinBox（不重写 stepBy，使用 setWrapping）
class DoubleSpinBox : public QSpinBox {
    Q_OBJECT
public:
    DoubleSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {}
    QString textFromValue(int val) const override {
        return QString("%1").arg(val, 2, 10, QChar('0'));
    }
    int valueFromText(const QString &text) const override {
        return text.toInt();
    }
};

// 进程清理排除列表
extern const QStringList excludedProcesses;

// 执行完全关机
bool PerformForceShutdown();

// 结束非系统进程，返回内存信息
struct MemInfo {
    quint64 totalPhysMB;
    quint64 freeBeforeMB;
    quint64 freeAfterMB;
    quint64 freedMB;
    int freePercent;
};
MemInfo killNonSystemProcesses();

// 写入窗口和文件日志
void logMessage(const QString &msg, QTextEdit *textEdit);

// 全局日志文件句柄
extern QFile *logFile;
extern QTextStream *logStream;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(bool silentMode, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTimedShutdown();
    void onImmediateShutdown();
    void onCleanup();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onExitProgram();
    void onTimerCheck();
    void onAutoStartChanged(Qt::CheckState state);

private:
    void setupUI();
    void createTrayIcon();
    void loadSettings();
    void saveSettings();
    void startTimerCheck();
    void performShutdownWithCleanup();
    void writeLog(const QString &msg);

    QTextEdit *logEdit;
    DoubleSpinBox *hourSpin;
    DoubleSpinBox *minuteSpin;
    QCheckBox *autoStartCheck;
    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
    QTimer *checkTimer;
    QSharedMemory *sharedMemory;
    QTimer *delayedShutdownTimer;
    QPointer<QMessageBox> shutdownConfirmBox;

    bool scheduledShutdownActive;
    int scheduledHour;
    int scheduledMinute;
    bool silentModeFlag;
};

#endif // MAIN_H