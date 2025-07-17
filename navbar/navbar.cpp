//
// Created by jmaidana on 7/17/25.
//

// You may need to build the project (run Qt uic code generator) to get "ui_Navbar.h" resolved

#include "navbar.h"
#include "ui_Navbar.h"


Navbar::Navbar(QWidget *parent) :
    QWidget(parent), ui(new Ui::Navbar) {
    ui->setupUi(this);
}

Navbar::~Navbar() {
    delete ui;
}
