#include "AnnotationEditor.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Icons.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace qaflow {

// ---- Renderizado -------------------------------------------------------------------------------

namespace {

constexpr double kHalfPi = 1.5707963267948966;

void drawArrow(QPainter& p, const Annotation& a) {
    const QPointF from = a.from, to = a.to;
    const double len = std::hypot(to.x() - from.x(), to.y() - from.y());
    if (len < 1) return;
    p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(a.color);
    const double head = std::clamp(len * 0.25, 8.0 + a.width * 2, 18.0 + a.width * 4);
    const double angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    const QPointF tip = to;
    const QPointF base = to - QPointF(std::cos(angle), std::sin(angle)) * head;
    const QPointF left = base + QPointF(std::cos(angle + kHalfPi), std::sin(angle + kHalfPi)) * (head * 0.45);
    const QPointF right = base + QPointF(std::cos(angle - kHalfPi), std::sin(angle - kHalfPi)) * (head * 0.45);
    p.drawLine(from, base);
    QPainterPath h;
    h.moveTo(tip); h.lineTo(left); h.lineTo(right); h.closeSubpath();
    p.drawPath(h);
}

void drawText(QPainter& p, const Annotation& a) {
    if (a.text.isEmpty()) return;
    QFont f = p.font();
    f.setPixelSize(std::max(12, 10 + a.width * 4));
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QRect box = fm.boundingRect(QRect(0, 0, 4000, 4000), Qt::AlignLeft | Qt::TextWordWrap, a.text);
    const QRectF bg(a.from, QSizeF(box.width() + 16, box.height() + 10));
    QPainterPath bp;
    bp.addRoundedRect(bg, 6, 6);
    p.setPen(Qt::NoPen);
    p.fillPath(bp, QColor(0, 0, 0, 165));
    p.setPen(a.color);
    p.drawText(bg.adjusted(8, 5, -8, -5), Qt::AlignLeft | Qt::TextWordWrap, a.text);
}

void drawBlur(QPainter& p, const QImage& base, const Annotation& a) {
    const QRect r = a.rect().toRect().intersected(base.rect());
    if (r.width() < 2 || r.height() < 2) return;
    // Pixelado: reducir a bloques de ~12 px y volver a ampliar sin suavizado.
    const int block = std::clamp(std::max(r.width(), r.height()) / 18, 6, 24);
    const QImage small = base.copy(r).scaled(std::max(1, r.width() / block), std::max(1, r.height() / block), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    p.drawImage(r, small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

} // namespace

QImage renderAnnotations(const QImage& base, const QList<Annotation>& items) {
    QImage out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    for (const auto& a : items) {
        switch (a.tool) {
            case Annotation::Tool::Arrow: drawArrow(p, a); break;
            case Annotation::Tool::Rectangle:
                p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
                p.setBrush(Qt::NoBrush);
                p.drawRect(a.rect());
                break;
            case Annotation::Tool::Ellipse:
                p.setPen(QPen(a.color, a.width));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(a.rect());
                break;
            case Annotation::Tool::Highlight: {
                QColor c = a.color; c.setAlpha(90);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawRect(a.rect());
                break;
            }
            case Annotation::Tool::Text: drawText(p, a); break;
            case Annotation::Tool::Blur: drawBlur(p, base, a); break;
        }
    }
    return out;
}

// ---- Lienzo ------------------------------------------------------------------------------------

/// Muestra la imagen a escala y traduce el ratón a coordenadas de la imagen.
class AnnotationCanvas : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationCanvas(const QImage& image, QWidget* parent = nullptr) : QWidget(parent), m_base(image) {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
    }

    const QImage& base() const { return m_base; }
    const QList<Annotation>& items() const { return m_items; }
    QImage rendered() const { return renderAnnotations(m_base, m_items); }
    void add(const Annotation& a) { m_items.append(a); update(); emit changed(); }
    void undo() { if (!m_items.isEmpty()) { m_items.removeLast(); update(); emit changed(); } }
    void setZoom(double z) { m_zoom = std::clamp(z, 0.05, 6.0); resize(sizeHint()); update(); }
    double zoom() const { return m_zoom; }
    void setTool(Annotation::Tool t) { m_tool = t; setCursor(t == Annotation::Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor); }
    Annotation::Tool tool() const { return m_tool; }
    void setColor(const QColor& c) { m_color = c; }
    QColor color() const { return m_color; }
    void setWidth(int w) { m_width = w; }
    int width() const { return m_width; }

    QSize sizeHint() const override { return (QSizeF(m_base.size()) * m_zoom).toSize().expandedTo(QSize(1, 1)); }

signals:
    void changed();
    void textRequested(const QPointF& at);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
        QList<Annotation> items = m_items;
        if (m_dragging) items.append(m_draft);
        const QImage frame = renderAnnotations(m_base, items);
        p.drawImage(QRectF(QPointF(0, 0), QSizeF(frame.size()) * m_zoom), frame);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const QPointF at = toImage(e->position());
        if (m_tool == Annotation::Tool::Text) { emit textRequested(at); return; }
        m_dragging = true;
        m_draft = Annotation{m_tool, at, at, {}, m_color, m_width};
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_dragging) return;
        m_draft.to = toImage(e->position());
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || !m_dragging) return;
        m_dragging = false;
        m_draft.to = toImage(e->position());
        const QRectF r = m_draft.rect();
        const bool tiny = m_tool == Annotation::Tool::Arrow ? QLineF(m_draft.from, m_draft.to).length() < 4 : (r.width() < 3 || r.height() < 3);
        if (!tiny) add(m_draft);
        else update();
    }

private:
    QPointF toImage(const QPointF& widgetPos) const {
        return QPointF(std::clamp(widgetPos.x() / m_zoom, 0.0, static_cast<double>(m_base.width())),
                       std::clamp(widgetPos.y() / m_zoom, 0.0, static_cast<double>(m_base.height())));
    }

    QImage m_base;
    QList<Annotation> m_items;
    Annotation m_draft;
    Annotation::Tool m_tool = Annotation::Tool::Arrow;
    QColor m_color = QColor(0xef, 0x44, 0x44);
    int m_width = 3;
    double m_zoom = 1.0;
    bool m_dragging = false;
};

// ---- Editor ------------------------------------------------------------------------------------

namespace {
/// Botón cuadrado con el glifo; el color del icono lo fija `updateToolButtons()` (activo o no).
QPushButton* toolButton(icons::Glyph glyph, const QString& tip) {
    auto* b = ui::button(QString(), "tool");
    b->setFixedSize(32, 32);
    b->setIconSize(QSize(20, 20));
    b->setIcon(QIcon(icons::pixmap(glyph, theme::TextSoft, 20)));
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setProperty("glyph", static_cast<int>(glyph));
    return b;
}

/// Trazo horizontal del grosor `width`: el icono de cada botón de grosor.
QIcon strokeIcon(int width, const QString& color) {
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(20, 20) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(color), width, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(4, 10), QPointF(16, 10));
    return QIcon(pm);
}

QFrame* separator() {
    auto* line = new QFrame;
    line->setProperty("role", QStringLiteral("editor-sep"));
    line->setFixedSize(1, 24);
    return line;
}
} // namespace

AnnotationEditor::AnnotationEditor(const QImage& image, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Anotar captura"));
    setModal(true);
    setStyleSheet(QStringLiteral("QDialog{background:%1;}").arg(theme::Bg));

    auto* v = ui::vbox(this, 0, 0);
    // Barra: herramientas · color y grosor · deshacer y ajustar · cancelar y guardar. Sólo iconos
    // (con tooltip y atajo) para que quepa en pantallas pequeñas.
    auto* bar = new QFrame;
    bar->setProperty("role", QStringLiteral("editor-bar"));
    auto* h = ui::hbox(bar, 10, 4);
    h->setContentsMargins(10, 8, 10, 8);

    m_canvas = new AnnotationCanvas(image);

    const struct { Annotation::Tool tool; icons::Glyph glyph; QString tip; } tools[] = {
        {Annotation::Tool::Arrow, icons::Glyph::Arrow, tr("Flecha (A)")},
        {Annotation::Tool::Rectangle, icons::Glyph::Rectangle, tr("Rectángulo (R)")},
        {Annotation::Tool::Ellipse, icons::Glyph::Ellipse, tr("Elipse (E)")},
        {Annotation::Tool::Highlight, icons::Glyph::Highlight, tr("Resaltar una zona (M)")},
        {Annotation::Tool::Text, icons::Glyph::Text, tr("Texto: clic donde quieras escribir (T)")},
        {Annotation::Tool::Blur, icons::Glyph::Blur, tr("Pixelar datos sensibles (D)")},
    };
    for (const auto& t : tools) {
        auto* b = toolButton(t.glyph, t.tip);
        b->setProperty("tool", static_cast<int>(t.tool));
        connect(b, &QPushButton::clicked, this, [this, tool = t.tool]() { setTool(tool); });
        h->addWidget(b);
        m_toolButtons << b;
    }
    h->addSpacing(4);
    h->addWidget(separator());
    h->addSpacing(4);
    const QString colors[] = {QStringLiteral("#ef4444"), QStringLiteral("#f59e0b"), QStringLiteral("#10b981"),
                              QStringLiteral("#3b82f6"), QStringLiteral("#ffffff"), QStringLiteral("#111827")};
    for (const auto& c : colors) {
        auto* b = new QPushButton;
        b->setFixedSize(20, 20);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("color", c);
        b->setToolTip(tr("Color"));
        connect(b, &QPushButton::clicked, this, [this, c]() { m_canvas->setColor(QColor(c)); updateToolButtons(); });
        h->addWidget(b);
        m_colorButtons << b;
    }
    h->addSpacing(6);
    // Grosor del trazo (y tamaño del texto): fino, medio o grueso.
    const struct { int width; QString tip; } strokes[] = {
        {2, tr("Trazo fino")}, {3, tr("Trazo medio")}, {6, tr("Trazo grueso")},
    };
    for (const auto& st : strokes) {
        auto* b = ui::button(QString(), "tool");
        b->setFixedSize(28, 32);
        b->setIconSize(QSize(20, 20));
        b->setToolTip(st.tip);
        b->setAccessibleName(st.tip);
        b->setProperty("stroke", st.width);
        connect(b, &QPushButton::clicked, this, [this, w = st.width]() { m_canvas->setWidth(w); updateToolButtons(); });
        h->addWidget(b);
        m_strokeButtons << b;
    }
    h->addStretch(1);
    m_undo = toolButton(icons::Glyph::Undo, tr("Deshacer la última anotación (Ctrl+Z)"));
    connect(m_undo, &QPushButton::clicked, this, &AnnotationEditor::undo);
    h->addWidget(m_undo);
    auto* fit = toolButton(icons::Glyph::Fit, tr("Ajustar a la ventana (0)"));
    connect(fit, &QPushButton::clicked, this, &AnnotationEditor::fitToWindow);
    h->addWidget(fit);
    h->addSpacing(4);
    h->addWidget(separator());
    h->addSpacing(4);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    cancel->setToolTip(tr("Cerrar sin guardar (Esc)"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    h->addWidget(cancel);
    auto* save = ui::button(tr("Guardar"), "primary");
    save->setObjectName(QStringLiteral("annotationSave"));
    save->setToolTip(tr("Sustituye la captura por la versión anotada (Ctrl+S)"));
    save->setDefault(true);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    h->addWidget(save);
    v->addWidget(bar);

    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1;}").arg(theme::Bg));
    m_scroll->setWidget(m_canvas);
    m_scroll->viewport()->installEventFilter(this);
    v->addWidget(m_scroll, 1);

    // Pie: ayuda (se recorta si no cabe, nunca ensancha la ventana) y zoom actual.
    auto* status = new QFrame;
    status->setProperty("role", QStringLiteral("editor-status"));
    auto* sh = ui::hbox(status, 0, 8);
    sh->setContentsMargins(12, 5, 12, 5);
    m_hint = ui::label(tr("Arrastra para dibujar · Ctrl+Z deshace · Ctrl+rueda amplía · 0 ajusta"), "muted-sm");
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sh->addWidget(m_hint, 1);
    m_zoomLabel = ui::label(QString(), "muted-sm");
    sh->addWidget(m_zoomLabel);
    v->addWidget(status);

    connect(m_canvas, &AnnotationCanvas::changed, this, [this]() { m_undo->setEnabled(!m_canvas->items().isEmpty()); });
    connect(m_canvas, &AnnotationCanvas::textRequested, this, [this](const QPointF& at) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Texto"), tr("Texto de la anotación"), QString(), &ok);
        if (ok && !text.trimmed().isEmpty()) {
            Annotation a;
            a.tool = Annotation::Tool::Text;
            a.from = a.to = at;
            a.text = text.trimmed();
            a.color = m_canvas->color();
            a.width = m_canvas->width();
            m_canvas->add(a);
        }
    });
    m_undo->setEnabled(false);
    setTool(Annotation::Tool::Arrow);
    updateToolButtons();

    // Tamaño: el de la imagen más la barra y el pie, sin pasar del 85 % de la pantalla ni bajar del
    // mínimo que necesita la barra. Así una captura pequeña no abre una ventana enorme y una grande
    // no se sale de un portátil.
    if (QScreen* screen = parent ? parent->screen() : QApplication::primaryScreen()) {
        const QSize avail = screen->availableSize() * 0.85;
        const QSize chrome(2, bar->sizeHint().height() + status->sizeHint().height() + 2);
        const QSize wanted = image.size().scaled(avail - chrome, Qt::KeepAspectRatio).boundedTo(image.size()) + chrome;
        const QSize minimum(std::min(bar->minimumSizeHint().width(), avail.width()), std::min(420, avail.height()));
        resize(wanted.expandedTo(minimum).boundedTo(screen->availableSize()));
    }
}

void AnnotationEditor::setTool(Annotation::Tool tool) {
    m_canvas->setTool(tool);
    updateToolButtons();
}

void AnnotationEditor::updateToolButtons() {
    for (auto* b : m_toolButtons) {
        const bool active = b->property("tool").toInt() == static_cast<int>(m_canvas->tool());
        ui::setFlag(b, "active", active);
        b->setIcon(QIcon(icons::pixmap(static_cast<icons::Glyph>(b->property("glyph").toInt()), active ? theme::Blue : theme::TextSoft, 20)));
    }
    for (auto* b : m_colorButtons) {
        const QString c = b->property("color").toString();
        const bool active = QColor(c) == m_canvas->color();
        b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:%2;border-radius:10px;padding:0;}")
                             .arg(c, active ? QStringLiteral("2px solid ") + theme::Blue : QStringLiteral("1px solid ") + theme::Border));
    }
    for (auto* b : m_strokeButtons) {
        const int w = b->property("stroke").toInt();
        const bool active = w == m_canvas->width();
        ui::setFlag(b, "active", active);
        b->setIcon(strokeIcon(w, active ? theme::Blue : theme::TextSoft));
    }
}

void AnnotationEditor::updateZoomLabel() {
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(qRound(m_canvas->zoom() * 100)));
}

void AnnotationEditor::addAnnotation(const Annotation& a) { m_canvas->add(a); }
void AnnotationEditor::undo() { m_canvas->undo(); }
QImage AnnotationEditor::result() const { return m_canvas->rendered(); }
const QList<Annotation>& AnnotationEditor::annotations() const { return m_canvas->items(); }

void AnnotationEditor::fitToWindow() {
    m_fit = true;
    // Sin barras de desplazamiento: con la imagen ajustada no hacen falta.
    const QSize avail = m_scroll->maximumViewportSize() - QSize(2, 2);
    const QImage& img = m_canvas->base();
    if (img.isNull()) return;
    const double z = std::min(static_cast<double>(avail.width()) / img.width(), static_cast<double>(avail.height()) / img.height());
    m_canvas->setZoom(std::clamp(std::min(z, 1.0), 0.05, 1.0));
    updateZoomLabel();
}

void AnnotationEditor::zoomBy(double factor) {
    m_fit = false;
    m_canvas->setZoom(m_canvas->zoom() * factor);
    updateZoomLabel();
}

void AnnotationEditor::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (m_fit) fitToWindow();
}

void AnnotationEditor::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    // Al abrirse la ventana aún no tiene su tamaño definitivo: se ajusta cuando el layout ya se aplicó.
    QTimer::singleShot(0, this, [this]() { if (m_fit) fitToWindow(); });
}

bool AnnotationEditor::eventFilter(QObject* watched, QEvent* e) {
    if (watched == m_scroll->viewport() && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() & Qt::ControlModifier) {
            zoomBy(we->angleDelta().y() > 0 ? 1.15 : 1 / 1.15);
            return true;
        }
    }
    return QDialog::eventFilter(watched, e);
}

void AnnotationEditor::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) { undo(); return; }
    if (e->matches(QKeySequence::Save)) { accept(); return; }
    if (e->matches(QKeySequence::ZoomIn)) { zoomBy(1.25); return; }
    if (e->matches(QKeySequence::ZoomOut)) { zoomBy(1 / 1.25); return; }
    if (e->key() == Qt::Key_Escape) { reject(); return; }
    if (e->key() == Qt::Key_0) { fitToWindow(); return; }
    if (e->modifiers() == Qt::NoModifier) {
        switch (e->key()) {
            case Qt::Key_A: setTool(Annotation::Tool::Arrow); return;
            case Qt::Key_R: setTool(Annotation::Tool::Rectangle); return;
            case Qt::Key_E: setTool(Annotation::Tool::Ellipse); return;
            case Qt::Key_M: setTool(Annotation::Tool::Highlight); return;
            case Qt::Key_T: setTool(Annotation::Tool::Text); return;
            case Qt::Key_D: setTool(Annotation::Tool::Blur); return;
            default: break;
        }
    }
    QDialog::keyPressEvent(e);
}

QImage AnnotationEditor::edit(const QImage& image, QWidget* parent) {
    if (image.isNull()) return {};
    AnnotationEditor dlg(image, parent);
    if (dlg.exec() != QDialog::Accepted || dlg.annotations().isEmpty()) return {};
    return dlg.result();
}

} // namespace qaflow

#include "AnnotationEditor.moc"
