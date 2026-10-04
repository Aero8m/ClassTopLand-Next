#include <QApplication>
#include <QPushButton>
#include "src/Core/AppComposer/AppComposer.h"
int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    AppComposer app;
    if (!app.startApp()) return 1;
    return QApplication::exec();
}
