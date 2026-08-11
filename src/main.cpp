#include "mainwindow.h"

#include <QApplication>

#ifdef FLUENTUI_BUILD_STATIC_LIB
#  include <QtQml/qqmlextensionplugin.h>
#  if (QT_VERSION > QT_VERSION_CHECK(6, 2, 0))
Q_IMPORT_QML_PLUGIN(FluentUIPlugin)
#  endif
#  include <FluentUI.h>
#endif

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