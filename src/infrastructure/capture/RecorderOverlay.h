#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace qaflow {

/// Control flotante durante una grabación: tiempo transcurrido, botón «Detener» y Esc para
/// cancelar. Se coloca en una esquina de la pantalla fuera de la región grabada si es posible.
class RecorderOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RecorderOverlay(const QRect& screenGeometry, const QRect& avoid, QWidget* parent = nullptr);

    void setElapsed(int secs, int maxSecs);

signals:
    void stopRequested();
    void cancelRequested();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* m_time;
    QPushButton* m_stop;
};

} // namespace qaflow
