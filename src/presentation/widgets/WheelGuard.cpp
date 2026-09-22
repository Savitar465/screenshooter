#include "WheelGuard.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>

namespace qaflow {

void WheelGuard::install() {
    static WheelGuard* guard = nullptr;
    if (guard || !qApp) return;
    guard = new WheelGuard(qApp);
    qApp->installEventFilter(guard);
}

bool WheelGuard::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel
        && (qobject_cast<QComboBox*>(watched) || qobject_cast<QAbstractSpinBox*>(watched))) {
        // Consumido para el campo pero sin aceptar: Qt lo reenvía al padre, que desplaza la pantalla.
        event->ignore();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace qaflow
