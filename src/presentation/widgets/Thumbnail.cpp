#include "Thumbnail.h"

#include "core/models/TestCase.h"
#include "presentation/theme/Theme.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace qaflow {

Thumbnail::Thumbnail(const QString& imagePath, int step, int seed, QWidget* parent)
    : QWidget(parent), m_path(imagePath), m_step(step), m_seed(seed) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    reload();
}

void Thumbnail::reload() {
    m_pixmap = QPixmap();
    m_extension.clear();
    Screenshot s;
    s.path = m_path;
    m_animation = s.isAnimation();
    if (s.isImage()) {
        QImageReader reader(m_path);
        reader.setScaledSize(reader.size().scaled(480, 300, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (!img.isNull()) m_pixmap = QPixmap::fromImage(img);
    }
    if (m_pixmap.isNull() && QFileInfo::exists(m_path)) m_extension = s.extension().toUpper();
    setToolTip(QFileInfo(m_path).fileName());
    update();
}

void Thumbnail::setWidthHint(int w) { m_widthHint = w; setFixedHeight(heightForWidth(w)); }

QSize Thumbnail::sizeHint() const { return {m_widthHint, heightForWidth(m_widthHint)}; }

void Thumbnail::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_pressed = true;
    QWidget::mousePressEvent(e);
}

void Thumbnail::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_pressed && rect().contains(e->pos())) emit clicked();
    m_pressed = false;
    QWidget::mouseReleaseEvent(e);
}

void Thumbnail::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect(), 5, 5);
    p.setClipPath(clip);

    if (!m_pixmap.isNull()) {
        p.fillRect(rect(), QColor(theme::Field));
        const QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    } else if (!m_extension.isEmpty()) {
        // Fichero que no es imagen: icono de documento con la extensión.
        p.fillRect(rect(), QColor(theme::Field));
        const int h = std::min(height() * 6 / 10, 64), w = h * 3 / 4;
        const QRect doc((width() - w) / 2, (height() - h) / 2 - 4, w, h);
        QPainterPath page;
        page.moveTo(doc.left(), doc.top());
        page.lineTo(doc.right() - h / 4, doc.top());
        page.lineTo(doc.right(), doc.top() + h / 4);
        page.lineTo(doc.right(), doc.bottom());
        page.lineTo(doc.left(), doc.bottom());
        page.closeSubpath();
        p.setPen(QPen(QColor(theme::Muted), 1.5));
        p.setBrush(QColor(theme::Elevated));
        p.drawPath(page);
        QFont f = p.font(); f.setPixelSize(std::max(9, h / 5)); f.setBold(true); p.setFont(f);
        p.setPen(QColor(theme::Text));
        p.drawText(doc.adjusted(0, h / 4, 0, 0), Qt::AlignCenter, m_extension.left(5));
    } else {
        // Marcador de posición: degradado según el número de captura (como en el diseño).
        static const int hues[][2] = {{210, 45}, {160, 40}, {280, 35}, {30, 45}};
        const auto& h = hues[m_seed % 4];
        QLinearGradient g(rect().topLeft(), rect().bottomRight());
        g.setColorAt(0, QColor::fromHslF(h[0] / 360.0, h[1] / 100.0, 0.22));
        g.setColorAt(1, QColor::fromHslF(((h[0] + 40) % 360) / 360.0, h[1] / 100.0, 0.12));
        p.fillRect(rect(), g);
    }

    auto badge = [&](const QString& text, const QColor& bg, const QColor& fg, bool right) {
        QFont f = p.font(); f.setPixelSize(10); f.setBold(true); p.setFont(f);
        const QRect tr = p.fontMetrics().boundingRect(text).adjusted(-6, -1, 6, 1);
        QRect r(right ? width() - 6 - tr.width() : 6, 6, tr.width(), tr.height());
        QPainterPath bp; bp.addRoundedRect(r, 4, 4);
        QColor c(bg); c.setAlpha(217);
        p.fillPath(bp, c);
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, text);
    };
    // Etiqueta del paso (izquierda) y tipo (derecha) para grabaciones.
    badge(m_step > 0 ? tr("Paso %1").arg(m_step) : tr("Sin paso"), QColor(m_step > 0 ? theme::Bg : theme::Amber), QColor(m_step > 0 ? theme::Text : theme::Bg), false);
    if (m_animation) badge(QStringLiteral("GIF"), QColor(theme::Violet), QColor(theme::OnAccent), true);
}

} // namespace qaflow
