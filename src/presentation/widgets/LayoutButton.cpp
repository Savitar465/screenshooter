#include "LayoutButton.h"

#include <QLayout>

namespace qaflow {

QSize LayoutButton::sizeHint() const {
    if (!layout()) return QPushButton::sizeHint();
    return layout()->sizeHint().grownBy(contentsMargins());
}

QSize LayoutButton::minimumSizeHint() const {
    if (!layout()) return QPushButton::minimumSizeHint();
    return layout()->minimumSize().grownBy(contentsMargins());
}

bool LayoutButton::hasHeightForWidth() const { return layout() && layout()->hasHeightForWidth(); }

int LayoutButton::heightForWidth(int w) const {
    if (!layout()) return QPushButton::heightForWidth(w);
    const QMargins m = contentsMargins();
    return layout()->heightForWidth(w - m.left() - m.right()) + m.top() + m.bottom();
}

} // namespace qaflow
