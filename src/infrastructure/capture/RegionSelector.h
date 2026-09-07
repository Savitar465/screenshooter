#pragma once

#include <QPixmap>
#include <QRect>
#include <QWidget>

namespace qaflow {

/// Overlay a pantalla completa para elegir una región con el ratón. Esc cancela.
class RegionSelector : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelector(const QPixmap& background, QWidget* parent = nullptr);

signals:
    void regionSelected(const QRect& rect);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRect selection() const;

    QPixmap m_background;
    QPoint m_origin;
    QPoint m_current;
    bool m_dragging = false;
};

} // namespace qaflow
