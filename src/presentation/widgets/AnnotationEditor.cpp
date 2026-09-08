#include "AnnotationEditor.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QFontMetrics>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
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

AnnotationEditor::AnnotationEditor(const QImage& image, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Anotar captura"));
    setModal(true);
    setStyleSheet(QStringLiteral("QDialog{background:%1;}").arg(theme::Bg));
    if (QScreen* s = parent ? parent->screen() : QApplication::primaryScreen()) resize(s->availableSize() * 0.85);

    auto* v = ui::vbox(this, 0, 0);
    auto* bar = new QWidget;
    bar->setStyleSheet(QStringLiteral("background:%1;border-bottom:1px solid %2;").arg(theme::Panel, theme::Border));
    auto* h = ui::hbox(bar, 10, 6);

    m_canvas = new AnnotationCanvas(image);

    const struct { Annotation::Tool tool; QString label; QString tip; } tools[] = {
        {Annotation::Tool::Arrow, tr("Flecha"), tr("Flecha (A)")},
        {Annotation::Tool::Rectangle, tr("Rectángulo"), tr("Rectángulo (R)")},
        {Annotation::Tool::Ellipse, tr("Elipse"), tr("Elipse (E)")},
        {Annotation::Tool::Highlight, tr("Marcador"), tr("Resaltar una zona (M)")},
        {Annotation::Tool::Text, tr("Texto"), tr("Texto: clic donde quieras escribir (T)")},
        {Annotation::Tool::Blur, tr("Difuminar"), tr("Pixelar datos sensibles (D)")},
    };
    for (const auto& t : tools) {
        auto* b = ui::button(t.label, "chip-lg");
        b->setToolTip(t.tip);
        b->setProperty("tool", static_cast<int>(t.tool));
        connect(b, &QPushButton::clicked, this, [this, tool = t.tool]() { setTool(tool); });
        h->addWidget(b);
        m_toolButtons << b;
    }
    h->addSpacing(10);
    const QString colors[] = {QStringLiteral("#ef4444"), QStringLiteral("#f59e0b"), QStringLiteral("#10b981"),
                              QStringLiteral("#3b82f6"), QStringLiteral("#ffffff"), QStringLiteral("#111827")};
    for (const auto& c : colors) {
        auto* b = new QPushButton;
        b->setFixedSize(22, 22);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("color", c);
        b->setToolTip(tr("Color"));
        connect(b, &QPushButton::clicked, this, [this, c]() { m_canvas->setColor(QColor(c)); updateToolButtons(); });
        h->addWidget(b);
        m_colorButtons << b;
    }
    h->addSpacing(10);
    auto* widthLabel = ui::label(tr("Grosor"), "muted-sm");
    h->addWidget(widthLabel);
    m_width = new QSpinBox;
    m_width->setRange(1, 12);
    m_width->setValue(3);
    m_width->setFixedWidth(56);
    connect(m_width, &QSpinBox::valueChanged, this, [this](int w) { m_canvas->setWidth(w); });
    h->addWidget(m_width);
    h->addStretch(1);
    m_hint = ui::label(tr("Arrastra para dibujar · Ctrl+Z deshace · Ctrl+rueda amplía"), "muted-sm");
    h->addWidget(m_hint);
    h->addSpacing(10);
    m_undo = ui::button(tr("Deshacer"), "ghost");
    m_undo->setToolTip(tr("Deshacer la última anotación (Ctrl+Z)"));
    connect(m_undo, &QPushButton::clicked, this, &AnnotationEditor::undo);
    h->addWidget(m_undo);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    h->addWidget(cancel);
    auto* save = ui::button(tr("Guardar"), "primary");
    save->setToolTip(tr("Sustituye la captura por la versión anotada (Ctrl+S)"));
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
            a.width = m_width->value();
            m_canvas->add(a);
        }
    });
    m_undo->setEnabled(false);
    setTool(Annotation::Tool::Arrow);
    updateToolButtons();
}

void AnnotationEditor::setTool(Annotation::Tool tool) {
    m_canvas->setTool(tool);
    updateToolButtons();
}

void AnnotationEditor::updateToolButtons() {
    for (auto* b : m_toolButtons) ui::setFlag(b, "active", b->property("tool").toInt() == static_cast<int>(m_canvas->tool()));
    for (auto* b : m_colorButtons) {
        const QString c = b->property("color").toString();
        const bool active = QColor(c) == m_canvas->color();
        b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:%2;border-radius:11px;}").arg(c, active ? QStringLiteral("3px solid ") + theme::Blue : QStringLiteral("1px solid ") + theme::Border));
    }
}

void AnnotationEditor::addAnnotation(const Annotation& a) { m_canvas->add(a); }
void AnnotationEditor::undo() { m_canvas->undo(); }
QImage AnnotationEditor::result() const { return m_canvas->rendered(); }
const QList<Annotation>& AnnotationEditor::annotations() const { return m_canvas->items(); }

void AnnotationEditor::fitToWindow() {
    const QSize avail = m_scroll->viewport()->size() - QSize(2, 2);
    const QImage& img = m_canvas->base();
    if (img.isNull()) return;
    const double z = std::min(static_cast<double>(avail.width()) / img.width(), static_cast<double>(avail.height()) / img.height());
    m_canvas->setZoom(std::clamp(std::min(z, 1.0), 0.05, 1.0));
}

void AnnotationEditor::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    fitToWindow();
}

bool AnnotationEditor::eventFilter(QObject* watched, QEvent* e) {
    if (watched == m_scroll->viewport() && e->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(e);
        if (we->modifiers() & Qt::ControlModifier) {
            m_canvas->setZoom(m_canvas->zoom() * (we->angleDelta().y() > 0 ? 1.15 : 1 / 1.15));
            return true;
        }
    }
    return QDialog::eventFilter(watched, e);
}

void AnnotationEditor::keyPressEvent(QKeyEvent* e) {
    if (e->matches(QKeySequence::Undo)) { undo(); return; }
    if (e->matches(QKeySequence::Save)) { accept(); return; }
    if (e->matches(QKeySequence::ZoomIn)) { m_canvas->setZoom(m_canvas->zoom() * 1.25); return; }
    if (e->matches(QKeySequence::ZoomOut)) { m_canvas->setZoom(m_canvas->zoom() / 1.25); return; }
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
