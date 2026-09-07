#pragma once

#include <QPushButton>

namespace qaflow {

/// QPushButton que puede contener un layout con widgets hijos (filas de lista, tarjetas
/// clicables). QPushButton ignora su layout al calcular el tamaño; aquí lo respetamos.
class LayoutButton : public QPushButton {
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int w) const override;
};

} // namespace qaflow
