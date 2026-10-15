#pragma once

#include "core/models/TestCase.h"

#include <QDialog>
#include <QImage>
#include <QList>
#include <QMap>

class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;

namespace qaflow {

/// Lo que el visor cuenta del paso al que pertenece una evidencia: qué se hizo, qué resultó y la nota
/// que se dejó. El resultado se pinta en `color`.
struct ViewerStep {
    QString action;
    QString result;
    QString color;
    QString note;
};

/// Visor a tamaño completo de un conjunto de evidencias: la imagen en el centro con flechas para
/// pasar de una a otra (← →), zoom (rueda con Ctrl, +/-, 0 = ajustar, 1 = 100 %), arrastre para
/// desplazarse, copiar, anotar y abrir la carpeta. A la derecha, el paso de la evidencia y los datos
/// del fichero; abajo, la tira con todas, cuyas miniaturas se leen en segundo plano al enseñarse.
/// Los ficheros que no son imagen muestran su nombre y un botón para abrirlos con la aplicación
/// del sistema.
class ImageViewer : public QDialog {
    Q_OBJECT
public:
    ImageViewer(const QList<Screenshot>& shots, int index, QWidget* parent = nullptr);

    /// De qué son estas evidencias: el id del caso, su título y dónde se ejecutó (el ciclo). Sin
    /// contexto, la cabecera lleva el nombre del fichero.
    void setContext(const QString& id, const QString& title, const QString& subtitle);
    /// Los pasos, por número (1..N), para contar en el panel lateral a cuál pertenece cada evidencia.
    void setSteps(const QMap<int, ViewerStep>& steps);
    /// Ofrece quitar la evidencia a la vista (botón «Quitar» y tecla Supr): emite `removeRequested`, y
    /// quien la quita llama a `removeShot()` si de verdad se quitó.
    void setRemovable(bool on);
    /// Saca la evidencia del visor y pasa a la siguiente; sin ninguna, el visor se cierra.
    void removeShot(int shotId);

    const Screenshot& current() const { return m_shots[m_index]; }
    int currentIndex() const { return m_index; }
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
    void removeRequested(int shotId);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    QWidget* buildStrip();
    /// Vuelve a llenar la tira con las evidencias actuales.
    void fillStrip();
    void updateImage();
    void updateHeader();
    void updateInfo();
    void updateStrip();
    void placeNavButtons();

    QList<Screenshot> m_shots;
    int m_index = 0;
    QImage m_image;
    double m_zoom = 1.0;
    bool m_fit = true;
    QMap<int, ViewerStep> m_steps;
    bool m_hasContext = false;
    bool m_removable = false;

    QLabel* m_contextId;
    QLabel* m_title;
    QLabel* m_subtitle;
    QLabel* m_counter;
    QLabel* m_zoomLabel;
    QPushButton* m_prev;
    QPushButton* m_next;
    QPushButton* m_annotate;
    QPushButton* m_copy;
    QPushButton* m_remove;
    QWidget* m_stage;
    QScrollArea* m_scroll;
    QLabel* m_canvas;
    QWidget* m_filePanel;
    QLabel* m_fileName;
    QLabel* m_fileInfo;
    // Panel lateral
    QWidget* m_stepBox;
    QLabel* m_stepHead;
    QLabel* m_stepAction;
    QLabel* m_stepNote;
    QLabel* m_infoFile;
    QLabel* m_infoSize;
    QLabel* m_infoTaken;
    // Tira de miniaturas
    QWidget* m_strip;
    QScrollArea* m_stripScroll;
    QLabel* m_hint;
    QList<QFrame*> m_stripItems;
    QPoint m_dragOrigin;
    bool m_dragging = false;
};

} // namespace qaflow
