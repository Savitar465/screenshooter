#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;

namespace qaflow {

/// Anotación sobre una imagen, en coordenadas de la imagen original.
struct Annotation {
    enum class Tool { Arrow, Rectangle, Ellipse, Highlight, Text, Blur };
    Tool tool = Tool::Arrow;
    QPointF from;      // flecha: origen; resto: esquina inicial
    QPointF to;        // flecha: punta; resto: esquina opuesta
    QString text;      // sólo Text
    QColor color = QColor(0xef, 0x44, 0x44);
    int width = 3;     // grosor del trazo / tamaño relativo del texto

    QRectF rect() const { return QRectF(from, to).normalized(); }
};

/// Dibuja las anotaciones sobre `base` y devuelve la imagen resultante. Función pura, también
/// la usan los tests.
QImage renderAnnotations(const QImage& base, const QList<Annotation>& items);

class AnnotationCanvas;

/// Editor de anotaciones de una captura: flechas, rectángulos, elipses, marcador, texto y
/// difuminado de datos sensibles. Deshacer (Ctrl+Z), color y grosor. `Guardar` devuelve
/// `QDialog::Accepted` y `result()` la imagen anotada.
class AnnotationEditor : public QDialog {
    Q_OBJECT
public:
    explicit AnnotationEditor(const QImage& image, QWidget* parent = nullptr);

    QImage result() const;
    const QList<Annotation>& annotations() const;
    void setTool(Annotation::Tool tool);
    void addAnnotation(const Annotation& a);   // para tests y automatización
    void undo();

    /// Abre el editor sobre `image`; devuelve la imagen anotada o una imagen nula si se canceló.
    static QImage edit(const QImage& image, QWidget* parent);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void fitToWindow();
    void updateToolButtons();

    AnnotationCanvas* m_canvas;
    QScrollArea* m_scroll;
    QList<QPushButton*> m_toolButtons;
    QList<QPushButton*> m_colorButtons;
    QSpinBox* m_width;
    QPushButton* m_undo;
    QLabel* m_hint;
};

} // namespace qaflow
