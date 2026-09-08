#include "EvidencePreview.h"

#include "presentation/theme/Theme.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace qaflow {

namespace {
constexpr int kRadius = 10;
/// La imagen se lee reducida: el visor nunca la muestra más grande que esto.
constexpr int kMaxWidth = 1800;
constexpr int kMaxHeight = 1200;
} // namespace

EvidencePreview::EvidencePreview(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(220);
}

void EvidencePreview::setShot(const Screenshot& shot) {
    m_shot = shot;
    reload();
}

void EvidencePreview::setPlaceholder(const QString& text) {
    m_placeholder = text;
    update();
}

void EvidencePreview::reload() {
    m_pixmap = QPixmap();
    m_extension.clear();
    setCursor(m_shot.id ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(m_shot.id ? tr("%1 · clic para abrirla a tamaño completo").arg(m_shot.fileName) : QString());
    if (m_shot.isImage()) {
        QImageReader reader(m_shot.path);
        reader.setScaledSize(reader.size().scaled(kMaxWidth, kMaxHeight, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (!img.isNull()) m_pixmap = QPixmap::fromImage(img);
    }
    if (m_pixmap.isNull() && m_shot.id) m_extension = m_shot.extension().toUpper();
    update();
}

void EvidencePreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) m_pressed = true;
    QWidget::mousePressEvent(e);
}

void EvidencePreview::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_pressed && m_shot.id && rect().contains(e->pos())) emit clicked();
    m_pressed = false;
    QWidget::mouseReleaseEvent(e);
}

void EvidencePreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    p.setClipPath(clip);
    p.fillRect(rect(), QColor(theme::Field));

    if (!m_shot.id) {
        p.setPen(QColor(theme::Muted));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
    } else if (!m_pixmap.isNull()) {
        // Ajustada al hueco, sin recortar ni ampliarla más allá de su tamaño real.
        const QSize target = m_pixmap.size().scaled(size(), Qt::KeepAspectRatio).boundedTo(m_pixmap.size() * 2);
        const QPixmap scaled = m_pixmap.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    } else {
        QFont f = p.font();
        f.setPixelSize(34);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(theme::Muted));
        p.drawText(rect(), Qt::AlignCenter, m_extension.isEmpty() ? tr("sin vista previa") : m_extension.left(5));
    }

    // Marco y etiquetas: paso y nombre del fichero.
    p.setClipping(false);
    p.setPen(QPen(QColor(theme::Border), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    if (!m_shot.id) return;

    int x = 12;
    const auto chip = [&](const QString& text, const QColor& bg, const QColor& fg, bool mono) {
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(!mono);
        if (mono) f.setFamilies({QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")});
        p.setFont(f);
        const QRect box = p.fontMetrics().boundingRect(text).adjusted(-8, -4, 8, 4);
        const QRect r(x, 12, box.width(), box.height());
        QPainterPath bp;
        bp.addRoundedRect(r, 5, 5);
        QColor c(bg);
        c.setAlpha(230);
        p.fillPath(bp, c);
        p.setPen(fg);
        p.drawText(r, Qt::AlignCenter, text);
        x += r.width() + 6;
    };
    // Las etiquetas van sobre la imagen: color fijo oscuro para que se lean con cualquier tema.
    const QColor overlay(0x11, 0x15, 0x1c);
    chip(m_shot.step > 0 ? tr("Paso %1").arg(m_shot.step) : tr("Sin paso"),
         m_shot.step > 0 ? overlay : QColor(theme::Amber), m_shot.step > 0 ? QColor(0xf3, 0xf4, 0xf6) : QColor(0x11, 0x15, 0x1c), false);
    chip(m_shot.fileName, overlay, QColor(0xcb, 0xd2, 0xdc), true);
}

} // namespace qaflow
