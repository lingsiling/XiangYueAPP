#include "logdialog.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    LogDialog w;
    w.show();
    return QCoreApplication::exec();
}
