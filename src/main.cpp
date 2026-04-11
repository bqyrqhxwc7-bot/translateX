#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("TPX Writer"));
    a.setOrganizationName(QStringLiteral("sr291"));
    a.setOrganizationDomain(QStringLiteral("local.tpxwriter"));
    MainWindow w;
    w.show();
    return a.exec();
}