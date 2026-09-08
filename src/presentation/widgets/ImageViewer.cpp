#include "ImageViewer.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>

namespace qaflow {

ImageViewer::ImageViewer(const QList<Screenshot>& shots, int index, QWidget* parent)
    : QDialog(parent), m_shots(shots), m_index(std::clamp(index, 0, std::max(0, static_cast<int>(shots.size()) - 1))) {
    setWindowTitle(tr("Evidencia"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral("QDialog{background:%1;}").arg(theme::Bg));
    if (QScreen* s = parent ? parent->screen() : QApplication::primaryScreen()) resize(s->availableSize() * 0.85);

    auto* v = ui::vbox(this, 0, 0);
    auto* bar = new QWidget;
    bar->setStyleSheet(QStringLiteral("background:%1;border-bottom:1px solid %2;").arg(theme::Panel, theme::Border));
    auto* h = ui::hbox(bar, 10, 8);
    m_prev = ui::button(QStringLiteral("◀"), "outline");
    m_prev->setToolTip(tr("Anterior (←)"));
    connect(m_prev, &QPushButton::clicked, this, [this]() { showIndex(m_index - 1); });
    h->addWidget(m_prev);
    m_next = ui::button(QStringLiteral("▶"), "outline");
    m_next->setToolTip(tr("Siguiente (→)"));
    connect(m_next, &QPushButton::clicked, this, [this]() { showIndex(m_index + 1); });
    h->addWidget(m_next);
    m_counter = ui::label(QString(), "mono-muted");
    h->addWidget(m_counter);
    m_title = ui::label(QString(), "h2");
    m_title->setStyleSheet(QStringLiteral("font-size:14px;"));
    h->addWidget(m_title, 1);
    m_zoomLabel = ui::label(QString(), "mono-muted");
    h->addWidget(m_zoomLabel);
    auto* fit = ui::button(tr("Ajustar"), "ghost");
    fit->setToolTip(tr("Ajustar a la ventana (0)"));
    connect(fit, &QPushButton::clicked, this, &ImageViewer::fitToWindow);
    h->addWidget(fit);
    auto* real = ui::button(QStringLiteral("100 %"), "ghost");
    real->setToolTip(tr("Tamaño real (1)"));
    connect(real, &QPushButton::clicked, this, [this]() { setZoom(1.0); });
    h->addWidget(real);
    m_copy = ui::button(tr("Copiar"), "outline");
    m_copy->setToolTip(tr("Copiar la imagen al portapapeles (Ctrl+C)"));
    connect(m_copy, &QPushButton::clicked, this, [this]() { emit copyRequested(current().id); });
    h->addWidget(m_copy);
    m_annotate = ui::button(tr("Anotar…"), "primary");
    m_annotate->setToolTip(tr("Flechas, rectángulos, texto y difuminado (Ctrl+E)"));
    connect(m_annotate, &QPushButton::clicked, this, [this]() { emit annotateRequested(current().id); });
    h->addWidget(m_annotate);
    auto* folder = ui::button(tr("Carpeta"), "ghost");
    folder->setToolTip(tr("Mostrar en la carpeta de capturas"));
    connect(folder, &QPushButton::clicked, this, [this]() { emit openFolderRequested(current().id); });
    h->addWidget(folder);
    auto* close = ui::button(QStringLiteral("×"), "icon");
    close->setToolTip(tr("Cerrar (Esc)"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    h->addWidget(close);
    v->addWidget(bar);

    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1;}").arg(theme::Bg));
    m_canvas = new QLabel;
    m_canvas->setAlignment(Qt::AlignCenter);
    m_canvas->setCursor(Qt::OpenHandCursor);
    m_canvas->installEventFilter(this);
    m_scroll->setWidget(m_canvas);
    m_scroll->viewport()->installEventFilter(this);
    v->addWidget(m_scroll, 1);

    // Ficheros que no son imagen
    m_filePanel = new QWidget;
    auto* fv = ui::vbox(m_filePanel, 40, 10);
    fv->addStretch(1);
    m_fileName = ui::label(QString(), "h1-sm");
    m_fileName->setAlignment(Qt::AlignCenter);
    fv->addWidget(m_fileName);
    m_fileInfo = ui::label(QString(), "muted");
    m_fileInfo->setAlignment(Qt::AlignCenter);
    fv->addWidget(m_fileInfo);
    auto* open = ui::button(tr("Abrir con la aplicación del sistema"), "primary");
    connect(open, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(current().path)); });
    fv->addWidget(open, 0, Qt::AlignCenter);
    fv->addStretch(1);
    v->addWidget(m_filePanel, 1);

    showIndex(m_index);
}

void ImageViewer::showIndex(int index) {
    if (m_shots.isEmpty()) return;
    m_index = std::clamp(index, 0, static_cast<int>(m_shots.size()) - 1);
    m_fit = true;
    reload();
}

void ImageViewer::reload() {
    const Screenshot& s = current();
    m_image = s.isImage() ? QImage(s.path) : QImage();
    const bool image = !m_image.isNull();
    m_scroll->setVisible(image);
    m_filePanel->setVisible(!image);
    m_annotate->setVisible(image && !s.isAnimation());
    m_copy->setVisible(image);
    if (!image) {
        const QFileInfo info(s.path);
        m_fileName->setText(info.fileName());
        m_fileInfo->setText(info.exists() ? tr("%1 · %2").arg(QLocale().formattedDataSize(info.size()), info.lastModified().toString(Qt::TextDate))
                                          : tr("El fichero ya no está en el disco"));
    }
    updateHeader();
    if (m_fit) fitToWindow();
    else updateImage();
}

void ImageViewer::updateHeader() {
    const Screenshot& s = current();
    m_title->setText(s.fileName + (s.step > 0 ? tr("  ·  Paso %1").arg(s.step) : QString()));
    m_counter->setText(QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_shots.size()));
    m_prev->setEnabled(m_index > 0);
    m_next->setEnabled(m_index < m_shots.size() - 1);
    setWindowTitle(tr("Evidencia · %1").arg(s.fileName));
}

void ImageViewer::setZoom(double zoom) {
    m_fit = false;
    m_zoom = std::clamp(zoom, 0.05, 8.0);
    updateImage();
}

void ImageViewer::fitToWindow() {
    m_fit = true;
    if (m_image.isNull()) return;
    const QSize avail = m_scroll->viewport()->size() - QSize(2, 2);
    const double z = std::min(static_cast<double>(avail.width()) / m_image.width(), static_cast<double>(avail.height()) / m_image.height());
    m_zoom = std::clamp(std::min(z, 1.0), 0.05, 1.0);   // ajustar nunca amplía
    updateImage();
}

void ImageViewer::updateImage() {
    if (m_image.isNull()) { m_zoomLabel->clear(); return; }
    const QSize target = (QSizeF(m_image.size()) * m_zoom).toSize().expandedTo(QSize(1, 1));
    m_canvas->setPixmap(QPixmap::fromImage(m_image.scaled(target, Qt::KeepAspectRatio, m_zoom < 1.0 ? Qt::SmoothTransformation : Qt::FastTransformation)));
    m_canvas->resize(target);
    m_zoomLabel->setText(QStringLiteral("%1 %  ·  %2 × %3").arg(qRound(m_zoom * 100)).arg(m_image.width()).arg(m_image.height()));
}

void ImageViewer::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (m_fit) fitToWindow();
}

void ImageViewer::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        setZoom(m_zoom * (e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15));
        e->accept();
        return;
    }
    QDialog::wheelEvent(e);
}

bool ImageViewer::eventFilter(QObject* watched, QEvent* e) {
    if (watched == m_scroll->viewport() && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() & Qt::ControlModifier) { wheelEvent(we); return true; }
    }
    if (watched == m_canvas) {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) { m_dragging = true; m_dragOrigin = me->globalPosition().toPoint(); m_canvas->setCursor(Qt::ClosedHandCursor); return true; }
        } else if (e->type() == QEvent::MouseMove && m_dragging) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint d = me->globalPosition().toPoint() - m_dragOrigin;
            m_dragOrigin = me->globalPosition().toPoint();
            m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() - d.x());
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() - d.y());
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            m_canvas->setCursor(Qt::OpenHandCursor);
        } else if (e->type() == QEvent::MouseButtonDblClick) {
            if (m_fit) setZoom(1.0); else fitToWindow();
            return true;
        }
    }
    return QDialog::eventFilter(watched, e);
}

void ImageViewer::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape: close(); return;
        case Qt::Key_Left: case Qt::Key_PageUp: showIndex(m_index - 1); return;
        case Qt::Key_Right: case Qt::Key_PageDown: case Qt::Key_Space: showIndex(m_index + 1); return;
        case Qt::Key_Home: showIndex(0); return;
        case Qt::Key_End: showIndex(m_shots.size() - 1); return;
        case Qt::Key_Plus: case Qt::Key_Equal: setZoom(m_zoom * 1.25); return;
        case Qt::Key_Minus: setZoom(m_zoom / 1.25); return;
        case Qt::Key_0: fitToWindow(); return;
        case Qt::Key_1: setZoom(1.0); return;
        case Qt::Key_C: if (e->modifiers() & Qt::ControlModifier && m_copy->isVisible()) { emit copyRequested(current().id); return; } break;
        case Qt::Key_E: if (e->modifiers() & Qt::ControlModifier && m_annotate->isVisible()) { emit annotateRequested(current().id); return; } break;
        default: break;
    }
    QDialog::keyPressEvent(e);
}

} // namespace qaflow
