#include "transferdialog.h"
#include "ui_transferdialog.h"
#include <QDebug>
#include <QMessageBox>
#include <QFile>

/**
 * TransferDialog 构造函数
 * 初始化对话框，创建定时器，连接信号槽
 */
TransferDialog::TransferDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TransferDialog)
    , m_transferType(Download)
    , m_state(Idle)
    , m_totalSize(0)
    , m_transferred(0)
    , m_lastTransferred(0)
    , m_averageSpeed(0.0)
    , m_isPaused(false)
{
    //设置 UI
    ui->setupUi(this);

    // 加载样式表
    QFile file(":/qss/transferdialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    //创建定时器（每 500ms 更新一次速度显示）
    m_speedTimer = new QTimer(this);
    connect(m_speedTimer, &QTimer::timeout, this, &TransferDialog::onTimerTimeout);

    //连接按钮信号
    connect(ui->btnCancel, &QPushButton::clicked, this, &TransferDialog::onBtnCancelClicked);

    //设置初始状态
    m_state = Idle;
    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(100);
}

/**
 * TransferDialog 析构函数
 * 停止定时器，释放资源
 */
TransferDialog::~TransferDialog()
{
    //停止定时器
    if (m_speedTimer) {
        m_speedTimer->stop();
    }
    delete ui;
}

/**
 * 设置文件名并更新 UI 显示
 * fileName 文件名
 *
 * 会根据传输类型显示相应的图标（📥 下载/📤 上传）
 */
void TransferDialog::setFileName(const QString &fileName)
{
    //根据传输类型选择图标
    QString icon = (m_transferType == Download) ? "📥" : "📤";
    QString typeStr = (m_transferType == Download) ? "下载中" : "上传中";

    //更新标签
    ui->labelFileName->setText(QString("%1 %2: %3").arg(icon, typeStr, fileName));
}

/**
 * 设置传输类型
 * type 传输类型（Upload 或 Download）
 *
 * 会自动更新文件名标签中的图标
 */
void TransferDialog::setTransferType(TransferType type)
{
    m_transferType = type;
}

/**
 * 更新传输进度
 * fileName 文件名
 * transferred 已传输字节数
 * total 总字节数
 * percentage 百分比（0-100）
 *
 * 此函数应在文件传输时不断被调用，用以更新进度条和相关信息
 *
 * 计算逻辑：
 * 1. 更新进度条
 * 2. 更新传输大小标签
 * 3. 启动定时器（如果还未启动）
 * 4. 保存当前状态供速度计算使用
 */
void TransferDialog::updateProgress(const QString &fileName, qint64 transferred, qint64 total, int percentage)
{
    //如果是第一次调用，初始化
    if (m_state == Idle || m_state == Completed || m_state == Cancelled) {
        m_state = Transferring;
        m_totalSize = total;
        m_transferred = 0;
        m_lastTransferred = 0;
        m_averageSpeed = 0.0;
        m_startTime = QTime::currentTime();
        m_speedTimer->start(500);  // 每 500ms 更新一次

        setFileName(fileName);
    }

    //更新已传输大小
    m_transferred = transferred;

    //更新进度条
    ui->progressBar->setValue(percentage);
    if (percentage == 100) {
        ui->progressBar->setFormat("100%");
    } else {
        ui->progressBar->setFormat(QString("%1%").arg(percentage));
    }

    //更新传输大小标签
    QString transferredStr = formatBytes(transferred);
    QString totalStr = formatBytes(total);
    ui->labelTransferred->setText(QString("已传输: %1 / 总大小: %2").arg(transferredStr, totalStr));

    //如果进度达到 100%，自动完成
    if (percentage >= 100) {
        completeTransfer();
    }
}

/**
 * 完成传输
 * 停止定时器，更新状态，发出信号
 */
void TransferDialog::completeTransfer()
{
    if (m_state == Completed) {
        return;  //避免重复调用
    }

    m_state = Completed;
    m_speedTimer->stop();

    //更新进度条为 100%
    ui->progressBar->setValue(100);
    ui->progressBar->setFormat("100%");

    //更新标签
    QString icon = (m_transferType == Download) ? "📥" : "📤";
    QString typeStr = (m_transferType == Download) ? "下载完成" : "上传完成";
    ui->labelFileName->setText(QString("%1 %2").arg(icon, typeStr));

    //禁用取消按钮
    ui->btnCancel->setEnabled(false);
    ui->btnCancel->setText("已完成");

    //发出完成信号
    emit transferCompleted();

    qDebug() << "[TransferDialog] 传输完成";

    //进度条加载完成后直接关闭对话框
    this->accept();
}

/**
 * 处理取消按钮点击事件
 * 停止传输，发出取消信号，关闭对话框
 */
void TransferDialog::onBtnCancelClicked()
{
    //确认取消
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "确认取消",
        "确定要取消此次传输吗？",
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        return;
    }

    //更新状态
    m_state = Cancelled;
    m_speedTimer->stop();

    //发出取消信号
    emit cancelRequested();

    qDebug() << "[TransferDialog] 用户取消了传输";

    //关闭对话框
    this->reject();
}

/**
 * @brief 处理定时器超时事件（每 500ms 调用一次）
 * @details 更新传输速度和剩余时间显示
 *
 * 速度计算逻辑（使用平均速度）：
 * - 平均速度 = 已传输字节 / 经过时间
 * - 这比实时速度更稳定，不容易因网络抖动而剧烈波动
 *
 * 剩余时间计算逻辑：
 * - 剩余字节 = 总字节 - 已传输字节
 * - 剩余时间 = 剩余字节 / 平均速度
 */
void TransferDialog::onTimerTimeout()
{
    //计算经过的时间（秒）
    int elapsedSeconds = m_startTime.secsTo(QTime::currentTime());
    if (elapsedSeconds == 0) {
        elapsedSeconds = 1;  // 避免除以 0
    }

    //计算平均传输速度（MB/s）
    m_averageSpeed = (m_transferred / (1024.0 * 1024.0)) / elapsedSeconds;

    //计算剩余时间
    int remainingSeconds = calculateRemainingTime();

    // 格式化速度
    QString speedStr = QString::number(m_averageSpeed, 'f', 2);

    //格式化剩余时间
    QString remainingStr;
    if (remainingSeconds <= 0) {
        remainingStr = "计算中...";
    } else {
        remainingStr = formatTime(remainingSeconds);
    }

    //更新速度标签
    ui->labelSpeed->setText(QString("速度: %1 MB/s | 剩余: %2").arg(speedStr, remainingStr));

    qDebug() << QString("[TransferDialog] 速度: %1 MB/s, 剩余: %2").arg(speedStr, remainingStr);
}

/**
 * @brief 格式化字节大小
 * @param bytes 字节数
 * @return 格式化后的字符串
 *
 * 转换规则：
 * - 小于 1 KB: 显示为 "xxx B"
 * - 1 KB - 1 MB: 显示为 "xxx KB"
 * - 1 MB - 1 GB: 显示为 "xxx MB"
 * - 大于等于 1 GB: 显示为 "xxx GB"
 *
 * @example formatBytes(1536) → "1.5 KB"
 * @example formatBytes(1048576) → "1.0 MB"
 */
QString TransferDialog::formatBytes(qint64 bytes) const
{
    const qint64 KB = 1024;
    const qint64 MB = 1024 * KB;
    const qint64 GB = 1024 * MB;

    if (bytes >= GB) {
        return QString::number((double)bytes / GB, 'f', 2) + " GB";
    } else if (bytes >= MB) {
        return QString::number((double)bytes / MB, 'f', 2) + " MB";
    } else if (bytes >= KB) {
        return QString::number((double)bytes / KB, 'f', 2) + " KB";
    } else {
        return QString::number(bytes) + " B";
    }
}

/**
 * @brief 格式化时间（秒）
 * @param seconds 秒数
 * @return 格式化后的字符串
 *
 * 转换规则：
 * - 小于 60 秒: 显示为 "xxx 秒"
 * - 60-3600 秒: 显示为 "x 分 y 秒"
 * - 大于 3600 秒: 显示为 "x 小时 y 分 z 秒"
 *
 * @example formatTime(125) → "2 分 5 秒"
 * @example formatTime(3661) → "1 小时 1 分 1 秒"
 */
QString TransferDialog::formatTime(int seconds) const
{
    if (seconds < 60) {
        return QString("%1 秒").arg(seconds);
    }

    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    if (hours > 0) {
        return QString("%1 小时 %2 分 %3 秒").arg(hours).arg(minutes).arg(secs);
    } else {
        return QString("%1 分 %2 秒").arg(minutes).arg(secs);
    }
}

/**
 * @brief 计算剩余时间
 * @return 剩余秒数
 *
 * 计算逻辑：
 * - 剩余字节 = 总字节 - 已传输字节
 * - 平均速度 = 已传输字节 / 经过时间（MB/s）
 * - 剩余时间 = 剩余字节 / 平均速度
 */
int TransferDialog::calculateRemainingTime() const
{
    if (m_averageSpeed <= 0) {
        return -1;  // 无法计算
    }

    qint64 remainingBytes = m_totalSize - m_transferred;
    if (remainingBytes <= 0) {
        return 0;
    }

    //转换为 MB 后计算
    double remainingMB = remainingBytes / (1024.0 * 1024.0);
    int remainingSeconds = (int)(remainingMB / m_averageSpeed);

    return remainingSeconds;
}