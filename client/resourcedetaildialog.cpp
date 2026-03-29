#include "resourcedetaildialog.h"
#include "ui_resourcedetaildialog.h"
#include <QMessageBox>

ResourceDetailDialog::ResourceDetailDialog(QWidget *parent,const QString &resourceName,FileClient *fileClient)
    : QDialog(parent), ui(new Ui::ResourceDetailDialog), m_resourceName(resourceName), m_fileClient(fileClient)
{
    ui->setupUi(this);

    //显示资源名
    ui->labelResourceName->setText(m_resourceName);
}

ResourceDetailDialog::~ResourceDetailDialog()
{
    delete ui;
}

void ResourceDetailDialog::on_buttonDownload_clicked()
{
    if(!m_fileClient)
    {
        QMessageBox::warning(this, "错误", "下载模块未初始化");
        return;
    }
    if(m_resourceName.isEmpty())
    {
        QMessageBox::warning(this, "错误", "资源名为空，无法下载");
        return;
    }

    m_fileClient->downloadFile(m_resourceName);
}

void ResourceDetailDialog::on_buttonComment_clicked()
{
    //先不管留言功能：这里先留空即可，保证能编译
}