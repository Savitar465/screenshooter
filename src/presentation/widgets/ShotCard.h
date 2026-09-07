#pragma once

#include "core/models/TestCase.h"

#include <QFrame>

namespace qaflow {

/// Tarjeta de evidencia: miniatura + selector de paso + mover/eliminar.
/// `Layout::Grid` es la tarjeta vertical (Casos), `Layout::Row` la fila compacta (Ejecución),
/// `Layout::Compact` la tarjeta pequeña sin selector (Reportar bug).
class ShotCard : public QFrame {
    Q_OBJECT
public:
    enum class Layout { Grid, Row, Compact };

    ShotCard(const Screenshot& shot, const QList<TestStep>& steps, Layout layout, QWidget* parent = nullptr);

signals:
    void stepChanged(int shotId, int step);
    void moveRequested(int shotId, int delta);
    void removeRequested(int shotId);
};

} // namespace qaflow
