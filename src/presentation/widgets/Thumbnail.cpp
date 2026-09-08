#include "Thumbnail.h"

#include "presentation/theme/Theme.h"

#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace qaflow {

Thumbnail::Thumbnail(const QString& imagePath, int step, int seed, QWidget* parent)
    : QWidget(parent), m_step(step), m_seed(seed) {
    QImageReader reader(imagePath);
    reader.setScaledSize(reader.size().scaled(480, 300, Qt::KeepAspectRatio));
    const QImage img = reader.read();
    if (!img.isNull()) m_pixmap = QPixmap::fromImage(img);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Thumbnail::setWidthHint(int w) { m_widthHint = w; setFixedHeight(heightForWidth(w)); }

QSize Thumbnail::sizeHint() const { return {m_widthHint, heightForWidth(m_widthHint)}; }

void Thumbnail::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect(), 5, 5);
    p.setClipPath(clip);

    if (m_pixmap.isNull()) {
        // Marcador de posición: degradado según el número de captura (como en el diseño).
        static const int hues[][2] = {{210, 45}, {160, 40}, {280, 35}, {30, 45}};
        const auto& h = hues[m_seed % 4];
        QLinearGradient g(rect().topLeft(), rect().bottomRight());
        g.setColorAt(0, QColor::fromHslF(h[0] / 360.0, h[1] / 100.0, 0.22));
        g.setColorAt(1, QColor::fromHslF(((h[0] + 40) % 360) / 360.0, h[1] / 100.0, 0.12));
        p.fillRect(rect(), g);
    } else {
        p.fillRect(rect(), QColor(theme::Field));
        const QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    }

    // Etiqueta del paso
    const QString text = m_step > 0 ? tr("Paso %1").arg(m_step) : tr("Sin paso");
    QFont f = p.font(); f.setPixelSize(10); f.setBold(true); p.setFont(f);
    const QRect tr = p.fontMetrics().boundingRect(text).adjusted(-6, -1, 6, 1);
    QRect badge(6, 6, tr.width(), tr.height());
    QPainterPath bp; bp.addRoundedRect(badge, 4, 4);
    QColor badgeColor(m_step > 0 ? theme::Bg : theme::Amber);
    badgeColor.setAlpha(217);
    p.fillPath(bp, badgeColor);
    p.setPen(QColor(m_step > 0 ? theme::Text : theme::Bg));
    p.drawText(badge, Qt::AlignCenter, text);
}

} // namespace qaflow
