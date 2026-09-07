#include "FlashOverlay.h"

#include <QPainter>
#include <QVariantAnimation>

namespace qaflow {

FlashOverlay::FlashOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void FlashOverlay::flash() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    raise();
    show();
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(350);
    anim->setStartValue(0.9);
    anim->setEndValue(0.0);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_opacity = v.toReal(); update(); });
    connect(anim, &QVariantAnimation::finished, this, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FlashOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(255, 255, 255, static_cast<int>(m_opacity * 255)));
}

} // namespace qaflow
