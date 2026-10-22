#include "Ui.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Icons.h"
#include "presentation/widgets/LayoutButton.h"

#include <QApplication>
#include <QClipboard>
#include <QToolTip>

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollBar>
#include <QStyle>

namespace qaflow::ui {

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void setRole(QWidget* w, const char* role) {
    w->setProperty("role", QString::fromLatin1(role));
    repolish(w);
}

void setFlag(QWidget* w, const char* name, bool value) {
    if (w->property(name).toBool() == value && w->property(name).isValid()) return;
    w->setProperty(name, value);
    repolish(w);
}

QPushButton* copyButton(const QString& text, int size) {
    auto* b = new QPushButton;
    b->setObjectName(QStringLiteral("copyButton"));
    b->setIcon(QIcon(icons::pixmap(icons::Glyph::Copy, theme::Muted, size >= 24 ? 16 : 13)));
    b->setFixedSize(size, size);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAccessibleName(QCoreApplication::translate("ui", "Copiar"));
    // Estilo propio y completo: también va dentro de otros botones (la tarjeta), donde el de la app no llega.
    b->setStyleSheet(QStringLiteral("QPushButton{background:transparent;border:none;border-radius:6px;padding:0;}"
                                    "QPushButton:hover{background:%1;}")
                         .arg(theme::tint(theme::Muted, 50)));
    setCopyText(b, text);
    QObject::connect(b, &QPushButton::clicked, b, [b]() {
        QApplication::clipboard()->setText(b->property("copyText").toString());
        QToolTip::showText(b->mapToGlobal(QPoint(0, b->height())), QCoreApplication::translate("ui", "Copiado"), b);
    });
    return b;
}

void setCopyText(QPushButton* button, const QString& text) {
    button->setProperty("copyText", text);
    button->setToolTip(QCoreApplication::translate("ui", "Copiar «%1»").arg(elide(text, 60)));
}

QLabel* label(const QString& text, const char* role, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    if (role) l->setProperty("role", QString::fromLatin1(role));
    return l;
}

QLabel* pill(const QString& text, const QString& bg, const QString& fg, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", QStringLiteral("pill"));
    l->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(bg, fg));
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QPushButton* button(const QString& text, const char* role, QWidget* parent) {
    auto* b = new LayoutButton(text, parent);
    b->setProperty("role", QString::fromLatin1(role));
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QFrame* card(const char* role, QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setProperty("role", QString::fromLatin1(role));
    return f;
}

QFrame* accentBar(const QString& color, QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setProperty("role", QStringLiteral("accent"));
    f->setFixedWidth(5);
    f->setStyleSheet(QStringLiteral("background:%1;border-radius:2px;").arg(color));
    return f;
}

QFrame* dot(const QString& color, int size, QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setFixedSize(size, size);
    f->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;").arg(color).arg(size / 2));
    return f;
}

QWidget* hstretch(QWidget* parent) {
    auto* w = new QWidget(parent);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return w;
}

QVBoxLayout* vbox(QWidget* parent, int margin, int spacing) {
    auto* l = new QVBoxLayout(parent);
    l->setContentsMargins(margin, margin, margin, margin);
    l->setSpacing(spacing);
    return l;
}

namespace {

/// Scroll que, mientras cabe, pide el tamaño entero de su contenido (QScrollArea lo recorta a unas
/// pocas líneas): un diálogo se abre con su tamaño de siempre y sólo se desplaza si la pantalla no le
/// deja crecer tanto.
class ScrollBody : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override {
        const QWidget* w = widget();
        if (!w) return QScrollArea::sizeHint();
        const int frame = 2 * frameWidth();
        return w->sizeHint().expandedTo(w->minimumSizeHint()) + QSize(frame, frame);
    }

    QSize minimumSizeHint() const override {
        const QWidget* w = widget();
        if (!w || horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) return QScrollArea::minimumSizeHint();
        // A lo ancho no se desplaza: el contenido cabe entero, también con la barra vertical a la vista.
        const int width = w->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2 * frameWidth();
        return {width, QScrollArea::minimumSizeHint().height()};
    }
};

} // namespace

QHBoxLayout* hbox(QWidget* parent, int margin, int spacing) {
    auto* l = new QHBoxLayout(parent);
    l->setContentsMargins(margin, margin, margin, margin);
    l->setSpacing(spacing);
    return l;
}

QScrollArea* scrollArea(QWidget** content, QVBoxLayout** layout, QWidget* parent) {
    auto* sa = new QScrollArea(parent);
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget;
    auto* l = vbox(w);
    sa->setWidget(w);
    if (content) *content = w;
    if (layout) *layout = l;
    return sa;
}

QScrollArea* scrollable(QWidget* content, QWidget* parent) {
    auto* sa = new ScrollBody(parent);
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(content);
    return sa;
}

QVBoxLayout* scrollingBody(QWidget* dialog, int margin, int spacing, QVBoxLayout** footer) {
    auto* outer = vbox(dialog, 0, 0);
    auto* content = new QWidget;
    auto* body = vbox(content, margin, spacing);
    body->setContentsMargins(margin, margin, margin, spacing);
    auto* sa = scrollable(content);
    sa->setObjectName(QStringLiteral("dialogBody"));
    outer->addWidget(sa, 1);
    auto* foot = new QVBoxLayout;
    foot->setContentsMargins(margin, 0, margin, margin);
    foot->setSpacing(spacing);
    outer->addLayout(foot);
    if (footer) *footer = foot;
    return body;
}

void clearLayout(QLayout* layout) {
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) { w->hide(); w->deleteLater(); }
        if (QLayout* child = item->layout()) clearLayout(child);
        delete item;
    }
}

QString elide(const QString& s, int max) {
    return s.size() > max ? s.left(max) + QStringLiteral("…") : s;
}

QIcon appIcon() {
    static QIcon icon;
    if (!icon.isNull()) return icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size / 128.0;
        QPainterPath rect;
        rect.addRoundedRect(QRectF(8 * s, 8 * s, 112 * s, 112 * s), 26 * s, 26 * s);
        p.fillPath(rect, QColor(QStringLiteral("#0e1116")));
        p.setPen(QPen(QColor(QStringLiteral("#10b981")), 6 * s));
        p.setBrush(Qt::NoBrush);
        p.drawPath(rect);
        QPainterPath check;
        check.moveTo(34 * s, 66 * s);
        check.lineTo(54 * s, 86 * s);
        check.lineTo(94 * s, 42 * s);
        p.setPen(QPen(QColor(QStringLiteral("#10b981")), 12 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(check);
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace qaflow::ui
