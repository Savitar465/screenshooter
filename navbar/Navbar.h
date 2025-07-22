//
// Created by jmaidana on 7/17/25.
//

#ifndef NAVBAR_H
#define NAVBAR_H

#include <QWidget>


QT_BEGIN_NAMESPACE
namespace Ui { class Navbar; }
QT_END_NAMESPACE

class Navbar : public QWidget {
Q_OBJECT

public:
    explicit Navbar(QWidget *parent = nullptr);
    ~Navbar() override;

private:
    Ui::Navbar *ui;
};


#endif //NAVBAR_H
