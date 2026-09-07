#include "Toast.h"

#include "presentation/widgets/Ui.h"

#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>

namespace qaflow {

namespace {
constexpr int kPlainMs = 2600;
constexpr int kWithActionMs = 8000;
} // namespace

Toast::Toast(QWidget* parent) : QFrame(parent) {
    ui::setRole(this, "toast");
    auto* l = ui::hbox(this, 12, 12);
    m_label = new QLabel(this);
    l->addWidget(m_label);
    m_action = ui::button(QString(), "outline", this);
    m_action->setStyleSheet(QStringLiteral("padding:4px 10px;font-size:12px;border-radius:7px;"));
    m_action->hide();
    connect(m_action, &QPushButton::clicked, this, [this]() {
        auto fn = std::move(m_onAction);
        m_onAction = nullptr;
        hide();
        if (fn) fn();
    });
    l->addWidget(m_action);
    hide();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::show(const QString& message, const QString& accentColor) {
    m_onAction = nullptr;
    m_action->hide();
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

    m_timer.start(kPlainMs);
}

void Toast::show(const QString& message, const QString& accentColor, const QString& actionText, std::function<void()> action) {
    show(message, accentColor);
    m_onAction = std::move(action);
    m_action->setText(actionText);
    m_action->show();
    adjustSize();
    reposition();
    m_timer.start(kWithActionMs);
}

void Toast::reposition() {
    if (!parentWidget()) return;
    adjustSize();
    move(parentWidget()->width() - width() - 24, parentWidget()->height() - height() - 20);
}

} // namespace qaflow
