#pragma once

#include "core/models/TestCase.h"

#include <QFrame>

namespace qaflow {

class Thumbnail;

/// Tarjeta de evidencia: miniatura + selector de paso + mover/eliminar.
/// `Layout::Grid` es la tarjeta vertical (Casos), `Layout::Row` la fila compacta (Ejecución),
/// `Layout::Compact` la tarjeta pequeña sin selector (Reportar bug).
/// Un clic en la miniatura pide abrirla a tamaño completo; el botón de anotar y el menú
/// contextual (botón derecho) dan acceso al resto de acciones.
class ShotCard : public QFrame {
    Q_OBJECT
public:
    enum class Layout { Grid, Row, Compact };

    ShotCard(const Screenshot& shot, const QList<TestStep>& steps, Layout layout, QWidget* parent = nullptr);

    const Screenshot& shot() const { return m_shot; }
    /// Vuelve a leer la miniatura (tras anotar la imagen).
    void reloadThumbnail();

signals:
    void stepChanged(int shotId, int step);
    void moveRequested(int shotId, int delta);
    void removeRequested(int shotId);
    void openRequested(int shotId);
    void annotateRequested(int shotId);
    void copyRequested(int shotId);
    void openFolderRequested(int shotId);

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    Screenshot m_shot;
    Thumbnail* m_thumb = nullptr;
};

} // namespace qaflow
