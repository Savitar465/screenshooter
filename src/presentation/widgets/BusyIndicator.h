#pragma once

#include <QTimer>
#include <QWidget>

namespace qaflow {

/// Indicador de actividad: un arco que gira mientras algo está en marcha.
///
/// Sólo gira si el bucle de eventos corre, así que sirve para lo que espera a la red y para el trabajo
/// que se trocea en pasos (el cambio de proyecto). Tapar con él un bloqueo largo del hilo de la
/// interfaz sería peor que no ponerlo: se quedaría quieto y parecería que la aplicación se colgó.
///
/// Se para solo al esconderse, para no repintar lo que no se ve.
class BusyIndicator : public QWidget {
    Q_OBJECT
public:
    explicit BusyIndicator(int size = 16, QWidget* parent = nullptr);

    /// Color del arco; por defecto, el azul de la paleta activa.
    void setColor(const QString& color);
    void start();
    void stop();
    bool isRunning() const { return m_timer.isActive(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    QTimer m_timer;
    int m_angle = 0;
    QString m_color;
};

} // namespace qaflow
