#include "WrapCheckBox.h"

#include <QRegularExpression>
#include <QStyleOptionButton>
#include <QStylePainter>

#include <algorithm>

namespace qaflow {

namespace {
constexpr int kTextFlags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap;
} // namespace

WrapCheckBox::WrapCheckBox(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
    // Alto según el ancho: con el Fixed de QCheckBox el layout lo dejaría en una línea.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

int WrapCheckBox::indicatorSpace() const {
    QStyleOptionButton opt;
    initStyleOption(&opt);
    opt.text.clear();
    const QRect contents = style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, this);
    return contents.left() - opt.rect.left();
}

QSize WrapCheckBox::minimumSizeHint() const {
    int word = 0;
    for (const auto& w : text().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
        word = std::max(word, fontMetrics().horizontalAdvance(w));
    const int width = indicatorSpace() + word;
    return {width, heightForWidth(width)};
}

int WrapCheckBox::heightForWidth(int width) const {
    // El alto de una línea es el de la casilla normal; cada línea más, lo que mide de alto el texto.
    const int oneLine = QCheckBox::sizeHint().height();
    const int textWidth = std::max(1, width - indicatorSpace());
    const QFontMetrics fm = fontMetrics();
    const int textHeight = fm.boundingRect(QRect(0, 0, textWidth, QWIDGETSIZE_MAX), kTextFlags, text()).height();
    return oneLine + std::max(0, textHeight - fm.height());
}

void WrapCheckBox::paintEvent(QPaintEvent*) {
    QStylePainter p(this);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    const QString label = opt.text;
    opt.text.clear();
    p.drawControl(QStyle::CE_CheckBox, opt);   // la casilla (y el foco), sin texto
    QRect textRect = style()->subElementRect(QStyle::SE_CheckBoxContents, &opt, this);
    textRect.setRight(rect().right());
    p.drawItemText(textRect, kTextFlags, opt.palette, isEnabled(), label, QPalette::WindowText);
}

} // namespace qaflow
