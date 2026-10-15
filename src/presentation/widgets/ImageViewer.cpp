#include "ImageViewer.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Icons.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTransform>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>

namespace qaflow {

namespace {

constexpr int NavSize = 44;
constexpr int StripW = 120, StripH = 75;

QIcon chevron(bool left) {
    QPixmap p = icons::pixmap(icons::Glyph::ChevronRight, theme::Text, 20);
    if (left) {
        const qreal dpr = p.devicePixelRatio();
        p = p.transformed(QTransform().scale(-1, 1));
        p.setDevicePixelRatio(dpr);
    }
    return QIcon(p);
}

QLabel* infoKey(const QString& text) {
    auto* l = ui::label(text, "muted-sm");
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return l;
}

QLabel* infoValue() {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(theme::Text));
    return l;
}

} // namespace

ImageViewer::ImageViewer(const QList<Screenshot>& shots, int index, QWidget* parent)
    : QDialog(parent), m_shots(shots), m_index(std::clamp(index, 0, std::max(0, static_cast<int>(shots.size()) - 1))) {
    setWindowTitle(tr("Evidencia"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral("QDialog{background:%1;}"
                                 "QPushButton#viewerNav{background:%2;border:1px solid %3;border-radius:%4px;}"
                                 "QPushButton#viewerNav:hover{background:%5;}"
                                 "QPushButton#viewerNav:disabled{background:transparent;border-color:transparent;}")
                      .arg(theme::Bg, theme::tint(theme::Panel, 230), theme::Border)
                      .arg(NavSize / 2)
                      .arg(theme::Elevated));
    if (QScreen* s = parent ? parent->screen() : QApplication::primaryScreen()) resize(s->availableSize() * 0.85);

    auto* v = ui::vbox(this, 0, 0);

    // Cabecera: de qué son las evidencias, en qué evidencia se está y lo que se hace con ella.
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("viewerHeader"));
    bar->setStyleSheet(QStringLiteral("QFrame#viewerHeader{background:%1;border-bottom:1px solid %2;}").arg(theme::Panel, theme::Border));
    auto* h = ui::hbox(bar, 0, 10);
    h->setContentsMargins(18, 10, 12, 10);
    m_contextId = ui::label(QString(), "mono-muted");
    m_contextId->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(theme::Blue));
    h->addWidget(m_contextId);
    m_title = new QLabel;
    m_title->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;color:%1;").arg(theme::Text));
    m_title->setMinimumWidth(40);
    h->addWidget(m_title, 0);
    m_subtitle = ui::label(QString(), "muted-sm");
    m_subtitle->setMinimumWidth(20);
    h->addWidget(m_subtitle, 1);
    m_counter = ui::label(QString(), "muted-sm");
    m_counter->setObjectName(QStringLiteral("viewerCounter"));
    h->addWidget(m_counter);
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
    m_annotate = ui::button(tr("Anotar"), "outline");
    m_annotate->setObjectName(QStringLiteral("viewerAnnotate"));
    m_annotate->setToolTip(tr("Flechas, rectángulos, texto y difuminado (Ctrl+E)"));
    connect(m_annotate, &QPushButton::clicked, this, [this]() { emit annotateRequested(current().id); });
    h->addWidget(m_annotate);
    m_copy = ui::button(tr("Copiar"), "outline");
    m_copy->setToolTip(tr("Copiar la imagen al portapapeles (Ctrl+C)"));
    connect(m_copy, &QPushButton::clicked, this, [this]() { emit copyRequested(current().id); });
    h->addWidget(m_copy);
    auto* folder = ui::button(tr("Abrir carpeta"), "outline");
    folder->setToolTip(tr("Mostrar en la carpeta de capturas"));
    connect(folder, &QPushButton::clicked, this, [this]() { emit openFolderRequested(current().id); });
    h->addWidget(folder);
    m_remove = ui::button(tr("Quitar"), "outline");
    m_remove->setObjectName(QStringLiteral("viewerRemove"));
    m_remove->setToolTip(tr("Quitar esta evidencia (Supr)"));
    m_remove->setVisible(false);
    connect(m_remove, &QPushButton::clicked, this, [this]() { if (!m_shots.isEmpty()) emit removeRequested(current().id); });
    h->addWidget(m_remove);
    auto* close = ui::button(QString(), "icon");
    close->setIcon(QIcon(icons::pixmap(icons::Glyph::Close, theme::Muted, 18)));
    close->setFixedSize(34, 34);
    close->setToolTip(tr("Cerrar (Esc)"));
    close->setAccessibleName(tr("Cerrar visor"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    h->addWidget(close);
    v->addWidget(bar);

    // Centro: la evidencia, con las flechas a los lados, y el panel del paso a la derecha.
    auto* middle = new QWidget;
    auto* mh = ui::hbox(middle, 0, 0);
    m_stage = new QWidget;
    auto* sv = ui::vbox(m_stage, 0, 0);
    sv->setContentsMargins(NavSize + 28, 20, NavSize + 28, 20);
    m_stage->installEventFilter(this);

    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1;}").arg(theme::Bg));
    m_canvas = new QLabel;
    m_canvas->setAlignment(Qt::AlignCenter);
    m_canvas->setCursor(Qt::OpenHandCursor);
    m_canvas->installEventFilter(this);
    m_scroll->setWidget(m_canvas);
    m_scroll->viewport()->installEventFilter(this);
    sv->addWidget(m_scroll, 1);

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
    sv->addWidget(m_filePanel, 1);

    m_prev = new QPushButton(m_stage);
    m_next = new QPushButton(m_stage);
    for (auto* nav : {m_prev, m_next}) {
        nav->setObjectName(QStringLiteral("viewerNav"));
        nav->setFixedSize(NavSize, NavSize);
        nav->setFocusPolicy(Qt::NoFocus);
        nav->setCursor(Qt::PointingHandCursor);
        nav->setIconSize(QSize(20, 20));
    }
    m_prev->setIcon(chevron(true));
    m_prev->setToolTip(tr("Anterior (←)"));
    m_prev->setAccessibleName(tr("Anterior"));
    m_next->setIcon(chevron(false));
    m_next->setToolTip(tr("Siguiente (→)"));
    m_next->setAccessibleName(tr("Siguiente"));
    connect(m_prev, &QPushButton::clicked, this, [this]() { showIndex(m_index - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { showIndex(m_index + 1); });
    mh->addWidget(m_stage, 1);

    auto* aside = new QFrame;
    aside->setObjectName(QStringLiteral("viewerInfo"));
    aside->setStyleSheet(QStringLiteral("QFrame#viewerInfo{background:%1;border-left:1px solid %2;}").arg(theme::Panel, theme::Border));
    aside->setFixedWidth(280);
    auto* av = ui::vbox(aside, 0, 18);
    av->setContentsMargins(20, 20, 20, 20);
    m_stepBox = new QWidget;
    auto* stv = ui::vbox(m_stepBox, 0, 5);
    m_stepHead = ui::label(QString(), "eyebrow");
    m_stepHead->setObjectName(QStringLiteral("viewerStep"));
    stv->addWidget(m_stepHead);
    m_stepAction = new QLabel;
    m_stepAction->setWordWrap(true);
    m_stepAction->setStyleSheet(QStringLiteral("color:%1;").arg(theme::TextSoft));
    stv->addWidget(m_stepAction);
    m_stepNote = new QLabel;
    m_stepNote->setWordWrap(true);
    m_stepNote->setStyleSheet(QStringLiteral("font-style:italic;font-size:12.5px;color:%1;").arg(theme::Muted));
    stv->addWidget(m_stepNote);
    av->addWidget(m_stepBox);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    grid->addWidget(infoKey(tr("Archivo")), 0, 0);
    m_infoFile = infoValue();
    m_infoFile->setObjectName(QStringLiteral("viewerFile"));
    m_infoFile->setStyleSheet(m_infoFile->styleSheet() + QStringLiteral("font-family:\"Consolas\",\"DejaVu Sans Mono\",monospace;"));
    grid->addWidget(m_infoFile, 0, 1);
    grid->addWidget(infoKey(tr("Tamaño")), 1, 0);
    m_infoSize = infoValue();
    grid->addWidget(m_infoSize, 1, 1);
    grid->addWidget(infoKey(tr("Capturada")), 2, 0);
    m_infoTaken = infoValue();
    grid->addWidget(m_infoTaken, 2, 1);
    grid->setColumnStretch(1, 1);
    av->addLayout(grid);
    av->addStretch(1);
    mh->addWidget(aside);
    v->addWidget(middle, 1);

    v->addWidget(buildStrip());
    showIndex(m_index);
}

QWidget* ImageViewer::buildStrip() {
    m_strip = new QFrame;
    m_strip->setObjectName(QStringLiteral("viewerStrip"));
    m_strip->setStyleSheet(QStringLiteral("QFrame#viewerStrip{border-top:1px solid %1;}").arg(theme::Border));
    auto* h = ui::hbox(m_strip, 0, 16);
    h->setContentsMargins(18, 12, 18, 12);
    m_stripScroll = new QScrollArea;
    m_stripScroll->setFrameShape(QFrame::NoFrame);
    m_stripScroll->setFocusPolicy(Qt::NoFocus);
    m_stripScroll->setWidgetResizable(true);
    m_stripScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stripScroll->setFixedHeight(StripH + 4 + 14);   // la miniatura, su borde y la barra de desplazamiento
    h->addWidget(m_stripScroll, 1);
    m_hint = ui::label(tr("← →  navegar  ·  Esc  cerrar"), "muted-sm");
    h->addWidget(m_hint, 0, Qt::AlignVCenter);
    fillStrip();
    return m_strip;
}

void ImageViewer::fillStrip() {
    m_stripItems.clear();
    auto* content = new QWidget;
    auto* row = ui::hbox(content, 0, 10);
    for (int i = 0; i < m_shots.size(); ++i) {
        const Screenshot& s = m_shots[i];
        auto* item = new QFrame;
        item->setObjectName(QStringLiteral("viewerStripItem"));
        item->setFixedSize(StripW + 4, StripH + 4);
        item->setToolTip(s.step > 0 ? tr("Paso %1 · %2").arg(s.step).arg(s.fileName) : s.fileName);
        auto* iv = ui::vbox(item, 0, 0);
        iv->setContentsMargins(2, 2, 2, 2);
        // La miniatura se lee en segundo plano la primera vez que se enseña: la tira de un caso con
        // muchas capturas no frena la apertura del visor.
        auto* thumb = new Thumbnail(s.path, s.step, s.id);
        thumb->setFixedSize(StripW, StripH);
        thumb->setCursor(Qt::PointingHandCursor);
        connect(thumb, &Thumbnail::clicked, this, [this, i]() { showIndex(i); });
        iv->addWidget(thumb);
        row->addWidget(item);
        m_stripItems << item;
    }
    row->addStretch(1);
    m_stripScroll->setWidget(content);   // la anterior se borra
    // Con una sola evidencia no hay nada entre lo que moverse.
    m_strip->setVisible(m_shots.size() > 1);
}

void ImageViewer::setRemovable(bool on) {
    m_removable = on;
    m_remove->setVisible(on);
    m_hint->setText(on ? tr("← →  navegar  ·  Supr  quitar  ·  Esc  cerrar") : tr("← →  navegar  ·  Esc  cerrar"));
}

void ImageViewer::removeShot(int shotId) {
    const auto it = std::find_if(m_shots.cbegin(), m_shots.cend(), [shotId](const Screenshot& s) { return s.id == shotId; });
    if (it == m_shots.cend()) return;
    const int removed = int(it - m_shots.cbegin());
    m_shots.removeAt(removed);
    if (m_shots.isEmpty()) { close(); return; }
    fillStrip();
    // Se queda en la misma posición, que ahora es la siguiente (o la anterior, si era la última).
    showIndex(removed < m_index ? m_index - 1 : m_index);
}

void ImageViewer::setContext(const QString& id, const QString& title, const QString& subtitle) {
    m_hasContext = !id.isEmpty() || !title.isEmpty();
    m_contextId->setText(id);
    m_title->setText(title);
    m_subtitle->setText(subtitle.isEmpty() ? QString() : QStringLiteral("· ") + subtitle);
    updateHeader();
}

void ImageViewer::setSteps(const QMap<int, ViewerStep>& steps) {
    m_steps = steps;
    updateInfo();
}

void ImageViewer::showIndex(int index) {
    if (m_shots.isEmpty()) return;
    m_index = std::clamp(index, 0, static_cast<int>(m_shots.size()) - 1);
    m_fit = true;
    reload();
    updateStrip();
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
    updateInfo();
    if (m_fit) fitToWindow();
    else updateImage();
}

void ImageViewer::updateHeader() {
    if (m_shots.isEmpty()) return;
    const Screenshot& s = current();
    m_contextId->setVisible(m_hasContext && !m_contextId->text().isEmpty());
    m_subtitle->setVisible(m_hasContext && !m_subtitle->text().isEmpty());
    if (!m_hasContext) m_title->setText(s.fileName + (s.step > 0 ? tr("  ·  Paso %1").arg(s.step) : QString()));
    m_counter->setText(tr("%1 de %2").arg(m_index + 1).arg(m_shots.size()));
    m_prev->setEnabled(m_index > 0);
    m_next->setEnabled(m_index < m_shots.size() - 1);
    setWindowTitle(tr("Evidencia · %1").arg(s.fileName));
}

void ImageViewer::updateInfo() {
    if (m_shots.isEmpty()) return;
    const Screenshot& s = current();
    // A qué paso pertenece y qué resultó en él: es lo que se mira junto a la captura.
    if (s.step > 0) {
        const auto it = m_steps.constFind(s.step);
        const bool known = it != m_steps.cend();
        m_stepHead->setText(known && !it->result.isEmpty() ? tr("PASO %1 · %2").arg(s.step).arg(it->result.toUpper()) : tr("PASO %1").arg(s.step));
        m_stepHead->setStyleSheet(known && !it->color.isEmpty() ? QStringLiteral("color:%1;").arg(it->color) : QString());
        m_stepAction->setText(known ? it->action : QString());
        m_stepAction->setVisible(known && !it->action.isEmpty());
        m_stepNote->setText(known ? it->note : QString());
        m_stepNote->setVisible(known && !it->note.trimmed().isEmpty());
        m_stepBox->show();
    } else if (!m_steps.isEmpty()) {
        m_stepHead->setText(tr("SIN PASO"));
        m_stepHead->setStyleSheet(QString());
        m_stepAction->setText(tr("Evidencia de la ejecución entera, sin paso asignado."));
        m_stepAction->show();
        m_stepNote->hide();
        m_stepBox->show();
    } else {
        m_stepBox->hide();
    }

    const QFileInfo info(s.path);
    m_infoFile->setText(s.fileName.isEmpty() ? info.fileName() : s.fileName);
    QString size = info.exists() ? QLocale().formattedDataSize(info.size()) : tr("no está en el disco");
    if (!m_image.isNull()) size = tr("%1 × %2 · %3").arg(m_image.width()).arg(m_image.height()).arg(size);
    m_infoSize->setText(size);
    m_infoTaken->setText(info.exists() ? info.lastModified().toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"));
}

void ImageViewer::updateStrip() {
    for (int i = 0; i < m_stripItems.size(); ++i) {
        const bool on = i == m_index;
        m_stripItems[i]->setStyleSheet(QStringLiteral("QFrame#viewerStripItem{border:2px solid %1;border-radius:7px;}").arg(on ? theme::Blue : QStringLiteral("transparent")));
        m_stripItems[i]->setAccessibleName(on ? tr("%1, seleccionada").arg(m_shots[i].fileName) : m_shots[i].fileName);
    }
    if (m_index < m_stripItems.size()) m_stripScroll->ensureWidgetVisible(m_stripItems[m_index], 40, 0);
}

void ImageViewer::placeNavButtons() {
    const int y = (m_stage->height() - NavSize) / 2;
    m_prev->move(16, y);
    m_next->move(m_stage->width() - NavSize - 16, y);
    m_prev->raise();
    m_next->raise();
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
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(qRound(m_zoom * 100)));
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
    if (watched == m_stage && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) {
        placeNavButtons();
        if (m_fit) fitToWindow();
    }
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
        case Qt::Key_Delete: if (m_removable && !m_shots.isEmpty()) { emit removeRequested(current().id); return; } break;
        default: break;
    }
    QDialog::keyPressEvent(e);
}

} // namespace qaflow
