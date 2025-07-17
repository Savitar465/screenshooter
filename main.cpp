#include <QApplication>

#include "navbar/navbar.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    Navbar navbar;
    navbar.show();
    return QApplication::exec();
}
