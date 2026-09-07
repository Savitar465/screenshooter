#pragma once

#include <QFrame>
#include <QTimer>
#include <functional>

class QLabel;
class QPushButton;

namespace qaflow {

/// Aviso flotante abajo a la derecha del contenedor. Desaparece solo tras unos segundos.
class Toast : public QFrame {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent);
    void show(const QString& message, const QString& accentColor);
    /// Aviso con un botón de acción (p. ej. «Deshacer»). Permanece más tiempo.
    void show(const QString& message, const QString& accentColor, const QString& actionText, std::function<void()> action);
    void reposition();

private:
    QLabel* m_label;
    QPushButton* m_action;
    std::function<void()> m_onAction;
    QTimer m_timer;
};

} // namespace qaflow
