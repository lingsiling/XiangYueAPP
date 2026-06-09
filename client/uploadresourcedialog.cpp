#include "uploadresourcedialog.h"
#include "ui_uploadresourcedialog.h"

#include <QFile>

UploadResourceDialog::UploadResourceDialog(FileClient *fileClient, qint64 userId, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UploadResourceDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    // 加载 QSS 样式表
    QFile qssFile(":/qss/uploadresourcedialog_style.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(qssFile.readAll()));
        qssFile.close();
    }
}

UploadResourceDialog::~UploadResourceDialog()
{
    delete ui;
}
