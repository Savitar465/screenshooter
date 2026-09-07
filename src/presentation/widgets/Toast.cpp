#include "Toast.h"

#include "presentation/widgets/Ui.h"

#include <QLabel>
#include <QPropertyAnimation>

namespace qaflow {

Toast::Toast(QWidget* parent) : QFrame(parent) {
    ui::setRole(this, "toast");
    auto* l = ui::hbox(this, 12);
    m_label = new QLabel(this);
    l->addWidget(m_label);
    hide();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::show(const QString& message, const QString& accentColor) {
    m_label->setText(message);
    setStyleSheet(QStringLiteral("QFrame[role=\"toast\"]{border-left:4px solid %1;}").arg(accentColor));
    adjustSize();
    reposition();
    raise();
    QWidget::show();

    // Entrada deslizando 12 px hacia arriba (equivalente a la animación "toastin" del diseño).
    const QPoint target = pos();
    auto* anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(200);
    anim->setStartValue(target + QPoint(0, 12));
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    m_timer.start(2600);
}

void Toast::reposition() {
    if (!parentWidget()) return;
    adjustSize();
    move(parentWidget()->width() - width() - 24, parentWidget()->height() - height() - 20);
}

} // namespace qaflow
