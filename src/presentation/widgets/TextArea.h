#pragma once

#include <QPlainTextEdit>

namespace qaflow {

/// QPlainTextEdit con altura fija en líneas y señal de cambio de texto (equivalente a <textarea rows=N>).
class TextArea : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TextArea(int rows, QWidget* parent = nullptr);
    void setTextSilently(const QString& text);

signals:
    void edited(const QString& text);

private:
    bool m_silent = false;
};

} // namespace qaflow
