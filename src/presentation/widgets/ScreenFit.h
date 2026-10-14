#pragma once

#include <QObject>
#include <QRect>

class QWidget;

namespace qaflow {

/// Filtro de toda la aplicación: ninguna ventana ni diálogo sale más grande que la pantalla en la que
/// aparece. Los tamaños de las ventanas están en píxeles lógicos, y con el tamaño de la interfaz al
/// 150 % un diálogo de 760×640 no cabe en un portátil de 1366×768. Al mostrarse, su mínimo y su tamaño
/// se recortan al hueco útil del escritorio (sin las barras del sistema ni el marco de la ventana) y se
/// mete dentro de él; lo que deja de caber lo desplaza el scroll de cada diálogo o pantalla.
class ScreenFit : public QObject {
    Q_OBJECT
public:
    /// Lo instala una vez sobre la aplicación.
    static void install();

    /// Hueco que puede ocupar el contenido de `window` (sin el marco) en la pantalla en la que sale:
    /// la de su ventana padre si la tiene.
    static QRect available(const QWidget* window);
    /// Encoge el mínimo y el tamaño de `window` hasta que quepa, y la mueve dentro si ya estaba colocada.
    static void fit(QWidget* window);
    /// Lo mismo con un hueco dado (los tests simulan así una pantalla pequeña).
    static void fit(QWidget* window, const QRect& area);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using QObject::QObject;
};

} // namespace qaflow
