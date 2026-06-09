#ifndef UPLOADRESOURCEDIALOG_H
#define UPLOADRESOURCEDIALOG_H

#include <QDialog>

class FileClient;

namespace Ui {
class UploadResourceDialog;
}

class UploadResourceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UploadResourceDialog(FileClient *fileClient, qint64 userId, QWidget *parent = nullptr);
    ~UploadResourceDialog();

private:
    Ui::UploadResourceDialog *ui;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
};

#endif // UPLOADRESOURCEDIALOG_H
