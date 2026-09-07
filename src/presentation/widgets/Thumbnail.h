#pragma once

#include <QPixmap>
#include <QWidget>

namespace qaflow {

/// Miniatura 16:10 de una captura con la etiqueta del paso en la esquina.
class Thumbnail : public QWidget {
    Q_OBJECT
public:
    explicit Thumbnail(const QString& imagePath, int step, int seed, QWidget* parent = nullptr);
    void setWidthHint(int w);

    QSize sizeHint() const override;
    int heightForWidth(int w) const override { return w * 10 / 16; }
    bool hasHeightForWidth() const override { return true; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPixmap m_pixmap;
    int m_step;
    int m_seed;
    int m_widthHint = 200;
};

} // namespace qaflow
