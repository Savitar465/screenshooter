#pragma once

#include "core/models/TestCase.h"

#include <QPixmap>
#include <QWidget>

namespace qaflow {

/// Visor grande de la evidencia seleccionada en la pantalla de ejecución: la imagen ajustada al
/// hueco (sin recortar), con la etiqueta del paso y el nombre del fichero encima. Los ficheros que
/// no son imagen muestran su extensión. Un clic pide abrirla a tamaño completo (ImageViewer).
class EvidencePreview : public QWidget {
    Q_OBJECT
public:
    explicit EvidencePreview(QWidget* parent = nullptr);

    /// Evidencia a mostrar; `Screenshot{}` (id 0) deja el visor en su estado vacío.
    void setShot(const Screenshot& shot);
    int shotId() const { return m_shot.id; }
    /// Vuelve a leer el fichero (tras anotarlo).
    void reload();

    /// Mensaje del estado vacío ("aún no hay evidencias", "sin ejecución"…).
    void setPlaceholder(const QString& text);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    Screenshot m_shot;
    QPixmap m_pixmap;
    QString m_extension;   // sólo para ficheros que no son imagen
    QString m_placeholder;
    bool m_pressed = false;
};

} // namespace qaflow
