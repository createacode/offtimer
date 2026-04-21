#include "main.h"
#include <QApplication>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QRandomGenerator>
#include <QThread>
#include <QStringConverter>
#include <QStyleFactory>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

QFile *logFile = nullptr;
QTextStream *logStream = nullptr;

const QStringList excludedProcesses = {
    "explorer.exe", "svchost.exe", "csrss.exe", "winlogon.exe",
    "services.exe", "lsass.exe", "wininit.exe", "system",
    "smss.exe", "dwm.exe", "fontdrvhost.exe", "taskhostw.exe",
    "sihost.exe", "ctfmon.exe", "audiodg.exe", "spoolsv.exe",
    "msmpeng.exe", "SearchIndexer.exe", "WUDFHost.exe", "WmiPrvSE.exe",
    "dllhost.exe", "RuntimeBroker.exe", "SettingSyncHost.exe", "LogonUI.exe"
};

static void SetShutdownPriority()
{
    SetProcessShutdownParameters(0x3FF, 0);
}

bool PerformForceShutdown()
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        logMessage("OpenProcessToken 失败", nullptr);
        return false;
    }
    if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        logMessage("LookupPrivilegeValue 失败", nullptr);
        return false;
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0)) {
        CloseHandle(hToken);
        logMessage("AdjustTokenPrivileges 失败", nullptr);
        return false;
    }
    CloseHandle(hToken);

    logMessage("尝试使用 ExitWindowsEx 强制断电关机...", nullptr);
    if (ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER)) {
        logMessage("ExitWindowsEx 关机成功", nullptr);
        return true;
    }
    DWORD err1 = GetLastError();
    logMessage(QString("ExitWindowsEx 失败，错误码: %1，尝试 InitiateShutdown...").arg(err1), nullptr);

    DWORD ret = InitiateShutdown(NULL, NULL, 0, SHUTDOWN_FORCE_OTHERS | SHUTDOWN_FORCE_SELF, SHTDN_REASON_MAJOR_OTHER);
    if (ret == ERROR_SUCCESS) {
        logMessage("InitiateShutdown 关机成功", nullptr);
        return true;
    }
    logMessage(QString("InitiateShutdown 失败，错误码: %1，尝试系统命令...").arg(ret), nullptr);

    system("shutdown /s /f /t 0");
    logMessage("已调用 system shutdown 命令", nullptr);
    return true;
}

MemInfo killNonSystemProcesses()
{
    MemInfo info{};
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    info.totalPhysMB = memStatus.ullTotalPhys / (1024 * 1024);
    info.freeBeforeMB = memStatus.ullAvailPhys / (1024 * 1024);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        logMessage(QString("创建进程快照失败，错误码: %1").arg(GetLastError()), nullptr);
        return info;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hSnapshot, &pe)) {
        CloseHandle(hSnapshot);
        return info;
    }

    QString currentProcessName = QCoreApplication::applicationName() + ".exe";
    do {
        QString procName = QString::fromWCharArray(pe.szExeFile).toLower();
        if (procName.compare(currentProcessName, Qt::CaseInsensitive) == 0)
            continue;
        bool exclude = false;
        for (const QString &ex : excludedProcesses) {
            if (procName.compare(ex, Qt::CaseInsensitive) == 0) {
                exclude = true;
                break;
            }
        }
        if (exclude)
            continue;

        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
        if (hProcess) {
            if (pe.th32ProcessID < 100) {
                CloseHandle(hProcess);
                continue;
            }
            if (TerminateProcess(hProcess, 0)) {
                logMessage(QString("已终止进程: %1 (PID: %2)").arg(procName).arg(pe.th32ProcessID), nullptr);
            }
            CloseHandle(hProcess);
        }
    } while (Process32NextW(hSnapshot, &pe));
    CloseHandle(hSnapshot);

    QThread::msleep(500);
    GlobalMemoryStatusEx(&memStatus);
    info.freeAfterMB = memStatus.ullAvailPhys / (1024 * 1024);
    info.freedMB = (info.freeAfterMB > info.freeBeforeMB) ? (info.freeAfterMB - info.freeBeforeMB) : 0;
    info.freePercent = (int)((double)info.freeAfterMB / info.totalPhysMB * 100);
    return info;
}

void logMessage(const QString &msg, QTextEdit *textEdit)
{
    QString timestamp = QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss] ");
    QString fullMsg = timestamp + msg;
    if (textEdit) {
        textEdit->append(fullMsg);
        QTextCursor cursor = textEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        textEdit->setTextCursor(cursor);
    }
    if (logStream && logFile && logFile->isOpen()) {
        *logStream << fullMsg << "\n";
        logStream->flush();
    }
}

static void initLogFile()
{
    QDir dir;
    QString logDirPath = QCoreApplication::applicationDirPath() + "/日志";
    if (!dir.exists(logDirPath))
        dir.mkpath(logDirPath);
    QString dateStr = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString timeStr = QDateTime::currentDateTime().toString("hhmmss");
    int randNum = QRandomGenerator::global()->bounded(1000, 9999);
    QString fileName = QString("%1_%2_%3_日志.txt").arg(dateStr).arg(timeStr).arg(randNum);
    QString filePath = logDirPath + "/" + fileName;
    logFile = new QFile(filePath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        logStream = new QTextStream(logFile);
        logStream->setEncoding(QStringConverter::Utf8);
    } else {
        delete logFile;
        logFile = nullptr;
    }
}

static void closeLogFile()
{
    if (logStream) {
        logStream->flush();
        delete logStream;
        logStream = nullptr;
    }
    if (logFile) {
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
}

// 辅助函数：创建非模态自动关闭的消息框
static QMessageBox* createNonModalMessageBox(QWidget *parent, QMessageBox::Icon icon,
                                             const QString &title, const QString &text,
                                             QMessageBox::StandardButtons buttons,
                                             int autoCloseSeconds = 30)
{
    QMessageBox *msgBox = new QMessageBox(icon, title, text, buttons, parent);
    msgBox->setAttribute(Qt::WA_DeleteOnClose);
    msgBox->setModal(false);
    if (buttons & QMessageBox::Yes)
        msgBox->setDefaultButton(QMessageBox::Yes);
    else if (buttons & QMessageBox::Ok)
        msgBox->setDefaultButton(QMessageBox::Ok);
    QTimer::singleShot(autoCloseSeconds * 1000, msgBox, &QMessageBox::close);
    return msgBox;
}

MainWindow::MainWindow(bool silentMode, QWidget *parent)
    : QMainWindow(parent),
      delayedShutdownTimer(nullptr),
      shutdownConfirmBox(nullptr),
      scheduledShutdownActive(false),
      scheduledHour(0),
      scheduledMinute(0),
      silentModeFlag(silentMode)
{
    SetShutdownPriority();

    sharedMemory = new QSharedMemory("APP121411_SINGLE_INSTANCE_KEY", this);
    if (sharedMemory->attach()) {
        QMessageBox *msgBox = createNonModalMessageBox(nullptr, QMessageBox::Warning, "提示",
                                                       "程序已在运行中！", QMessageBox::Ok, 30);
        msgBox->show();
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    if (!sharedMemory->create(1)) {
        sharedMemory->detach();
        if (!sharedMemory->create(1)) {
            qDebug() << "共享内存创建失败";
        }
    }

    setupUI();
    createTrayIcon();
    loadSettings();
    startTimerCheck();

    writeLog(QStringLiteral("软件启动时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
    writeLog(QStringLiteral("电源离开模式: 已启用（系统允许离开模式）"));

    if (!silentModeFlag) {
        show();
    }
}

MainWindow::~MainWindow()
{
    if (sharedMemory && sharedMemory->isAttached())
        sharedMemory->detach();
}

void MainWindow::setupUI()
{
    setWindowTitle(QStringLiteral("定时关机程序 V1.15.0  —  Copyright © 2026.4 XAF"));
    setMinimumSize(525, 375);
    resize(525, 375);

    QIcon windowIcon(":/app.ico");
    if (!windowIcon.isNull())
        setWindowIcon(windowIcon);

    logEdit = new QTextEdit(this);
    logEdit->setReadOnly(true);
    logEdit->setPlaceholderText(QStringLiteral("操作日志将显示在此处..."));
    logEdit->setStyleSheet(
        "QTextEdit {"
        "   background-color: #fefefe;"
        "   color: #2c3e50;"
        "   font-family: 'Consolas', 'Monaco', monospace;"
        "   font-size: 11px;"
        "   border: 1px solid #dcdde1;"
        "   border-radius: 8px;"
        "   padding: 8px;"
        "}"
        "QTextEdit:focus {"
        "   border: 1px solid #3498db;"
        "}"
    );

    QGroupBox *timeGroup = new QGroupBox(QStringLiteral("⏰ 定时关机时间"), this);
    timeGroup->setStyleSheet(
        "QGroupBox {"
        "   font-weight: bold;"
        "   font-size: 12px;"
        "   border: 1px solid #bdc3c7;"
        "   border-radius: 10px;"
        "   margin-top: 8px;"
        "   padding-top: 8px;"
        "   background-color: #f8f9fa;"
        "}"
        "QGroupBox::title {"
        "   subcontrol-origin: margin;"
        "   left: 15px;"
        "   padding: 0 8px 0 8px;"
        "   color: #2c3e50;"
        "}"
    );

    QLabel *hourLabel = new QLabel(QStringLiteral("时"), this);
    hourLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2c3e50; margin-right: 0px;");

    hourSpin = new DoubleSpinBox(this);
    hourSpin->setRange(0, 23);
    hourSpin->setWrapping(true);
    hourSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    hourSpin->setFixedSize(80, 34);
    hourSpin->setAlignment(Qt::AlignCenter);
    hourSpin->setStyleSheet(
        "QSpinBox {"
        "   background-color: white;"
        "   border: 1px solid #ced6e0;"
        "   border-radius: 6px;"
        "   padding: 4px;"
        "   font-size: 14px;"
        "   font-weight: bold;"
        "}"
        "QSpinBox::up-button {"
        "   subcontrol-position: top right;"
        "   width: 16px;"
        "   height: 16px;"
        "}"
        "QSpinBox::down-button {"
        "   subcontrol-position: bottom right;"
        "   width: 16px;"
        "   height: 16px;"
        "}"
        "QSpinBox::up-button:hover { background-color: #e0e0e0; }"
        "QSpinBox::down-button:hover { background-color: #e0e0e0; }"
        "QSpinBox::up-button:pressed { background-color: #c0c0c0; }"
        "QSpinBox::down-button:pressed { background-color: #c0c0c0; }"
    );

    QLabel *colonLabel = new QLabel(":", this);
    colonLabel->setAlignment(Qt::AlignCenter);
    colonLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #2c3e50;");

    minuteSpin = new DoubleSpinBox(this);
    minuteSpin->setRange(0, 59);
    minuteSpin->setWrapping(true);
    minuteSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    minuteSpin->setFixedSize(80, 34);
    minuteSpin->setAlignment(Qt::AlignCenter);
    minuteSpin->setStyleSheet(hourSpin->styleSheet());

    QLabel *minuteLabel = new QLabel(QStringLiteral("分"), this);
    minuteLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #2c3e50; margin-left: 0px;");

    autoStartCheck = new QCheckBox(QStringLiteral("开机自启"), this);
    autoStartCheck->setStyleSheet(
        "QCheckBox {"
        "   spacing: 8px;"
        "   font-size: 12px;"
        "   font-weight: normal;"
        "   color: #2c3e50;"
        "}"
        "QCheckBox::indicator {"
        "   width: 18px;"
        "   height: 18px;"
        "   border-radius: 4px;"
        "   border: 2px solid #bdc3c7;"
        "   background-color: white;"
        "}"
        "QCheckBox::indicator:checked {"
        "   border: 2px solid white;"
        "   background-color: #27ae60;"
        "}"
    );

    QHBoxLayout *timeLayout = new QHBoxLayout;
    timeLayout->setSpacing(8);
    timeLayout->addStretch();
    timeLayout->addWidget(hourLabel);
    timeLayout->addWidget(hourSpin);
    timeLayout->addWidget(colonLabel);
    timeLayout->addWidget(minuteSpin);
    timeLayout->addWidget(minuteLabel);
    timeLayout->addSpacing(20);
    timeLayout->addWidget(autoStartCheck);
    timeLayout->addStretch();
    timeGroup->setLayout(timeLayout);

    QPushButton *btnTimed = new QPushButton(QStringLiteral("⏱ 定时关机"), this);
    QPushButton *btnImmediate = new QPushButton(QStringLiteral("⚡ 立即关机"), this);
    QPushButton *btnClean = new QPushButton(QStringLiteral("🧹 一键清理"), this);

    QString btnBaseStyle = 
        "QPushButton {"
        "   font-size: 13px;"
        "   font-weight: bold;"
        "   color: white;"
        "   border: 1px solid white;"
        "   border-radius: 8px;"
        "   padding: 12px 18px;"
        "   min-width: 110px;"
        "   background-color: #1565C0;"
        "}"
        "QPushButton:hover { background-color: #0D47A1; }"
        "QPushButton:pressed { padding-top: 14px; padding-bottom: 10px; }";

    btnTimed->setStyleSheet(btnBaseStyle);
    btnImmediate->setStyleSheet(btnBaseStyle.replace("#1565C0", "#C62828"));
    btnClean->setStyleSheet(btnBaseStyle.replace("#1565C0", "#2E7D32"));

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(btnTimed);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(btnImmediate);
    btnLayout->addSpacing(10);
    btnLayout->addWidget(btnClean);
    btnLayout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->addWidget(logEdit, 2);
    mainLayout->addWidget(timeGroup, 1);
    mainLayout->addLayout(btnLayout, 1);

    QWidget *central = new QWidget(this);
    central->setLayout(mainLayout);
    setCentralWidget(central);

    setStyleSheet(
        "QMainWindow {"
        "   background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ecf0f1, stop:1 #dfe6e9);"
        "}"
    );

    connect(btnTimed, &QPushButton::clicked, this, &MainWindow::onTimedShutdown);
    connect(btnImmediate, &QPushButton::clicked, this, &MainWindow::onImmediateShutdown);
    connect(btnClean, &QPushButton::clicked, this, &MainWindow::onCleanup);
    connect(autoStartCheck, &QCheckBox::checkStateChanged, this, &MainWindow::onAutoStartChanged);
}

void MainWindow::createTrayIcon()
{
    trayIcon = new QSystemTrayIcon(this);
    QIcon icon(":/app.ico");
    if (icon.isNull()) {
        QString iconPath = QCoreApplication::applicationDirPath() + "/app.ico";
        if (QFile::exists(iconPath))
            icon = QIcon(iconPath);
        else
            icon = QIcon::fromTheme("computer");
    }
    trayIcon->setIcon(icon);
    trayIcon->setToolTip(QStringLiteral("定时关机程序"));

    trayMenu = new QMenu(this);
    QAction *showAction = new QAction(QStringLiteral("打开主界面"), this);
    QAction *exitAction = new QAction(QStringLiteral("退出程序"), this);
    trayMenu->addAction(showAction);
    trayMenu->addAction(exitAction);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();

    connect(trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    connect(showAction, &QAction::triggered, this, &QWidget::show);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExitProgram);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::onExitProgram()
{
    QMessageBox *msgBox = createNonModalMessageBox(this, QMessageBox::Warning, "确认退出",
                                                   "定时关机将失效，确定退出程序吗？",
                                                   QMessageBox::Yes | QMessageBox::No, 30);
    msgBox->button(QMessageBox::Yes)->setText("是");
    msgBox->button(QMessageBox::No)->setText("否");
    connect(msgBox, &QMessageBox::finished, this, [this](int result) {
        if (result == QMessageBox::Yes) {
            closeLogFile();
            qApp->quit();
        }
    });
    msgBox->show();
}

void MainWindow::onTimedShutdown()
{
    scheduledHour = hourSpin->value();
    scheduledMinute = minuteSpin->value();
    scheduledShutdownActive = true;
    saveSettings();
    writeLog(QString("已设定定时关机时间: %1时%2分")
             .arg(scheduledHour, 2, 10, QChar('0'))
             .arg(scheduledMinute, 2, 10, QChar('0')));
    QMessageBox *infoBox = createNonModalMessageBox(this, QMessageBox::Information, "定时关机",
                                                    QString("已设定在 %1:%2 执行关机")
                                                    .arg(scheduledHour, 2, 10, QChar('0'))
                                                    .arg(scheduledMinute, 2, 10, QChar('0')),
                                                    QMessageBox::Ok, 30);
    infoBox->show();
}

void MainWindow::onImmediateShutdown()
{
    QMessageBox *msgBox = createNonModalMessageBox(this, QMessageBox::Warning, "立即关机",
                                                   "系统将清理进程后强制关机，是否继续？",
                                                   QMessageBox::Yes | QMessageBox::No, 30);
    msgBox->button(QMessageBox::Yes)->setText("是");
    msgBox->button(QMessageBox::No)->setText("否");
    connect(msgBox, &QMessageBox::finished, this, [this](int result) {
        if (result == QMessageBox::Yes) {
            writeLog("用户确认立即关机");
            performShutdownWithCleanup();
        } else if (result == QMessageBox::No) {
            writeLog("用户取消立即关机");
        } else {
            // 自动关闭（30秒无操作）
            writeLog("30秒无操作默认取消立即关机");
        }
    });
    msgBox->show();
}

void MainWindow::onCleanup()
{
    QMessageBox *msgBox = createNonModalMessageBox(this, QMessageBox::Question, "一键清理",
                                                   "将终止所有非系统用户进程，是否继续？",
                                                   QMessageBox::Yes | QMessageBox::No, 30);
    msgBox->button(QMessageBox::Yes)->setText("是");
    msgBox->button(QMessageBox::No)->setText("否");
    connect(msgBox, &QMessageBox::finished, this, [this](int result) {
        if (result == QMessageBox::Yes) {
            writeLog("开始执行一键清理...");
            MemInfo info = killNonSystemProcesses();
            writeLog(QString("系统总内存: %1 MB").arg(info.totalPhysMB));
            writeLog(QString("清理释放内存: %1 MB").arg(info.freedMB));
            writeLog(QString("清理后空闲内存占比: %1%").arg(info.freePercent));
            QMessageBox *resultBox = createNonModalMessageBox(this, QMessageBox::Information, "一键清理",
                                                              QString("释放内存 %1 MB，当前空闲内存占比 %2%")
                                                              .arg(info.freedMB).arg(info.freePercent),
                                                              QMessageBox::Ok, 30);
            resultBox->show();
        } else if (result == QMessageBox::No) {
            writeLog("用户取消一键清理");
        } else {
            // 自动关闭（30秒无操作）
            writeLog("30秒无操作默认取消一键清理");
        }
    });
    msgBox->show();
}

void MainWindow::performShutdownWithCleanup()
{
    writeLog("开始清理非系统进程...");
    MemInfo info = killNonSystemProcesses();
    writeLog(QString("释放内存: %1 MB").arg(info.freedMB));
    writeLog("执行系统关机...");
    PerformForceShutdown();
}

void MainWindow::onTimerCheck()
{
    if (!scheduledShutdownActive)
        return;
    QTime now = QTime::currentTime();
    int targetMinutes = scheduledHour * 60 + scheduledMinute;
    int nowMinutes = now.hour() * 60 + now.minute();
    if (qAbs(nowMinutes - targetMinutes) <= 1) {
        scheduledShutdownActive = false;
        writeLog("定时关机条件触发，准备关机");

        if (shutdownConfirmBox) {
            shutdownConfirmBox->close();
            delete shutdownConfirmBox;
            shutdownConfirmBox = nullptr;
        }
        if (delayedShutdownTimer) {
            delayedShutdownTimer->stop();
            delete delayedShutdownTimer;
            delayedShutdownTimer = nullptr;
        }

        shutdownConfirmBox = new QMessageBox(QMessageBox::Question, "定时关机",
                                             "定时关机时间已到，是否立即关机？\n若不操作，2分钟后将自动关机。",
                                             QMessageBox::Yes | QMessageBox::No, this);
        shutdownConfirmBox->setAttribute(Qt::WA_DeleteOnClose);
        shutdownConfirmBox->setModal(false);
        shutdownConfirmBox->button(QMessageBox::Yes)->setText("是");
        shutdownConfirmBox->button(QMessageBox::No)->setText("否");
        QTimer::singleShot(90000, shutdownConfirmBox, &QMessageBox::close);

        delayedShutdownTimer = new QTimer(this);
        delayedShutdownTimer->setSingleShot(true);
        connect(delayedShutdownTimer, &QTimer::timeout, this, [this]() {
            writeLog("延迟2分钟未操作，自动执行关机");
            performShutdownWithCleanup();
        });
        delayedShutdownTimer->start(120000);

        connect(shutdownConfirmBox, &QMessageBox::finished, this, [this](int result) {
            if (result == QMessageBox::Yes) {
                if (delayedShutdownTimer) {
                    delayedShutdownTimer->stop();
                    delayedShutdownTimer->deleteLater();
                    delayedShutdownTimer = nullptr;
                }
                writeLog("用户确认立即关机");
                performShutdownWithCleanup();
            } else if (result == QMessageBox::No) {
                if (delayedShutdownTimer) {
                    delayedShutdownTimer->stop();
                    delayedShutdownTimer->deleteLater();
                    delayedShutdownTimer = nullptr;
                }
                writeLog("用户取消定时关机");
            } else {
                writeLog("定时关机确认框超时自动关闭，2分钟后将自动关机");
            }
            shutdownConfirmBox = nullptr;
        });

        shutdownConfirmBox->show();
    }
}

void MainWindow::startTimerCheck()
{
    checkTimer = new QTimer(this);
    connect(checkTimer, &QTimer::timeout, this, &MainWindow::onTimerCheck);
    checkTimer->start(60000);
}

void MainWindow::loadSettings()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    int savedHour = settings.value("ShutdownHour", 22).toInt();
    int savedMinute = settings.value("ShutdownMinute", 0).toInt();
    hourSpin->setValue(savedHour);
    minuteSpin->setValue(savedMinute);

    autoStartCheck->blockSignals(true);
    bool autoStart = settings.value("AutoStart", false).toBool();
    autoStartCheck->setChecked(autoStart);
    autoStartCheck->blockSignals(false);

    scheduledShutdownActive = settings.value("ScheduledActive", false).toBool();
    scheduledHour = settings.value("ScheduledHour", 22).toInt();
    scheduledMinute = settings.value("ScheduledMinute", 0).toInt();

    if (scheduledShutdownActive) {
        writeLog(QString("上次设定定时关机时间: %1时%2分")
                 .arg(scheduledHour, 2, 10, QChar('0'))
                 .arg(scheduledMinute, 2, 10, QChar('0')));
    }
    writeLog(QString("配置加载完成，配置文件路径: %1").arg(configPath));
}

void MainWindow::saveSettings()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("ShutdownHour", hourSpin->value());
    settings.setValue("ShutdownMinute", minuteSpin->value());
    settings.setValue("AutoStart", autoStartCheck->isChecked());
    settings.setValue("ScheduledActive", scheduledShutdownActive);
    settings.setValue("ScheduledHour", scheduledHour);
    settings.setValue("ScheduledMinute", scheduledMinute);
}

void MainWindow::onAutoStartChanged(Qt::CheckState state)
{
    bool enable = (state == Qt::Checked);
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString appPath = QCoreApplication::applicationFilePath();
    appPath = QDir::toNativeSeparators(appPath);
    if (enable) {
        QString regValue = QString("\"%1\" --silent").arg(appPath);
        reg.setValue("APP121411", regValue);
        writeLog("已开启开机自启动");
    } else {
        reg.remove("APP121411");
        writeLog("已关闭开机自启动");
    }
    saveSettings();
}

void MainWindow::writeLog(const QString &msg)
{
    logMessage(msg, logEdit);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("windowsvista"));
    a.setQuitOnLastWindowClosed(false);

    bool silentMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--silent")
            silentMode = true;
    }

    initLogFile();

    if (silentMode) {
        QThread::sleep(5);
    }

    MainWindow w(silentMode);
    int ret = a.exec();
    closeLogFile();
    return ret;
}
