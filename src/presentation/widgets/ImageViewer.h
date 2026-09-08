#pragma once

#include "core/models/TestCase.h"

#include <QDialog>
#include <QImage>
#include <QList>

class QLabel;
class QPushButton;
class QScrollArea;

namespace qaflow {

/// Visor a tamaño completo de las evidencias de un caso: navegación entre ellas (← →), zoom
/// (rueda con Ctrl, +/-, 0 = ajustar, 1 = 100 %), arrastre para desplazarse, copiar, anotar y
/// abrir la carpeta. Los ficheros que no son imagen muestran su nombre y un botón para abrirlos
/// con la aplicación del sistema.
class ImageViewer : public QDialog {
    Q_OBJECT
public:
    ImageViewer(const QList<Screenshot>& shots, int index, QWidget* parent = nullptr);

    const Screenshot& current() const { return m_shots[m_index]; }
    void showIndex(int index);
    /// Vuelve a leer la imagen actual (tras anotarla).
    void reload();
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void fitToWindow();

signals:
    void annotateRequested(int shotId);
    void copyRequested(int shotId);
    void openFolderRequested(int shotId);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void updateImage();
    void updateHeader();

    QList<Screenshot> m_shots;
    int m_index = 0;
    QImage m_image;
    double m_zoom = 1.0;
    bool m_fit = true;

    QLabel* m_title;
    QLabel* m_counter;
    QLabel* m_zoomLabel;
    QPushButton* m_prev;
    QPushButton* m_next;
    QPushButton* m_annotate;
    QPushButton* m_copy;
    QScrollArea* m_scroll;
    QLabel* m_canvas;
    QWidget* m_filePanel;
    QLabel* m_fileName;
    QLabel* m_fileInfo;
    QPoint m_dragOrigin;
    bool m_dragging = false;
};

} // namespace qaflow
