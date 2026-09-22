#pragma once

#include <QObject>

namespace qaflow {

/// Filtro de toda la aplicación: la rueda del ratón no cambia el valor de los campos de selección
/// (combos, spin boxes y fechas). Al desplazarse por una pantalla, el cursor pasa por encima de ellos
/// y los iba modificando sin querer; ahora la rueda sigue hacia el contenedor y la pantalla se desplaza.
/// La lista desplegada de un combo sí se recorre con la rueda: no es el combo, sino su vista.
class WheelGuard : public QObject {
    Q_OBJECT
public:
    /// Lo instala una vez sobre la aplicación.
    static void install();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using QObject::QObject;
};

} // namespace qaflow
