#pragma once

#include <QWidget>

namespace qaflow {

/// Destello blanco breve al capturar pantalla.
class FlashOverlay : public QWidget {
    Q_OBJECT
public:
    explicit FlashOverlay(QWidget* parent);
    void flash();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal m_opacity = 0.0;
};

} // namespace qaflow
