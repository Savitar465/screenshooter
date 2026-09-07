#pragma once

#include <QFrame>
#include <QTimer>

class QLabel;

namespace qaflow {

/// Aviso flotante abajo a la derecha del contenedor. Desaparece solo tras unos segundos.
class Toast : public QFrame {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent);
    void show(const QString& message, const QString& accentColor);
    void reposition();

private:
    QLabel* m_label;
    QTimer m_timer;
};

} // namespace qaflow
