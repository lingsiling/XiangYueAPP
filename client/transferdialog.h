#ifndef TRANSFERDIALOG_H
#define TRANSFERDIALOG_H

#include <QDialog>
#include <QTimer>
#include <QTime>

namespace Ui {
class TransferDialog;
}

/**
 * @class TransferDialog
 * @brief 文件传输进度对话框（上传/下载）
 *
 * 功能特点：
 * - 实时显示文件传输进度
 * - 自动计算传输速度和剩余时间
 * - 支持暂停/继续/取消操作
 * - 低耦合设计，通过信号与外层通信
 * - 支持上传和下载两种模式
 *
 * 使用示例：
 * @code
 *   TransferDialog dlg;
 *   dlg.setFileName("document.pdf");
 *   dlg.setTransferType(TransferDialog::Download);
 *   connect(fileClient, &FileClient::downloadProgress, &dlg, &TransferDialog::updateProgress);
 *   dlg.exec();
 * @endcode
 */
class TransferDialog : public QDialog
{
    Q_OBJECT

public:
    // 传输类型枚举
    enum TransferType {
        Upload = 0,     // 上传
        Download = 1    // 下载
    };

    // 传输状态枚举
    enum TransferState {
        Idle = 0,           // 空闲
        Transferring = 1,   // 传输中
        Paused = 2,         // 暂停
        Completed = 3,      // 完成
        Cancelled = 4       // 已取消
    };

public:
    explicit TransferDialog(QWidget *parent = nullptr);
    ~TransferDialog();

    /**
     * @brief 设置文件名
     * @param fileName 要传输的文件名
     */
    void setFileName(const QString &fileName);

    /**
     * @brief 设置传输类型
     * @param type 传输类型（Upload 或 Download）
     */
    void setTransferType(TransferType type);

    /**
     * @brief 更新传输进度
     * @param fileName 文件名
     * @param transferred 已传输字节数
     * @param total 总字节数
     * @param percentage 百分比（0-100）
     */
    void updateProgress(const QString &fileName, qint64 transferred, qint64 total, int percentage);

    /**
     * @brief 获取当前传输状态
     * @return 传输状态
     */
    TransferState getState() const { return m_state; }

    /**
     * @brief 完成传输
     */
    void completeTransfer();

signals:
    /**
     * @brief 取消传输请求信号
     * 当用户点击"取消"按钮时发出
     */
    void cancelRequested();

    /**
     * @brief 暂停/继续传输请求信号
     * @param pause 是否暂停（true 为暂停，false 为继续）
     */
    void pauseRequested(bool pause);

    /**
     * @brief 传输完成信号
     */
    void transferCompleted();

private slots:
    /**
     * @brief 处理取消按钮点击事件
     */
    void onBtnCancelClicked();

    /**
     * @brief 处理定时器超时事件（用于更新速度显示）
     */
    void onTimerTimeout();

private:
    /**
     * @brief 格式化字节大小
     * @param bytes 字节数
     * @return 格式化后的字符串（如 "1.5 MB"）
     */
    QString formatBytes(qint64 bytes) const;

    /**
     * @brief 格式化时间（秒）
     * @param seconds 秒数
     * @return 格式化后的字符串（如 "1h 2m 30s"）
     */
    QString formatTime(int seconds) const;

    /**
     * @brief 计算剩余时间
     * @return 剩余秒数
     */
    int calculateRemainingTime() const;

private:
    Ui::TransferDialog *ui;

    TransferType m_transferType;    // 传输类型（上传/下载）
    TransferState m_state;          // 当前传输状态

    qint64 m_totalSize;             // 文件总大小（字节）
    qint64 m_transferred;           // 已传输大小（字节）

    QTime m_startTime;              // 传输开始时间
    QTimer *m_speedTimer;           // 定时器（用于更新速度）

    qint64 m_lastTransferred;       // 上次更新的传输大小
    double m_averageSpeed;          // 平均传输速度（MB/s）

    bool m_isPaused;                // 是否已暂停
};

#endif // TRANSFERDIALOG_H