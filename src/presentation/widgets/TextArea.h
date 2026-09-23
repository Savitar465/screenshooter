#pragma once

#include <QPlainTextEdit>

class QPushButton;

namespace qaflow {

/// QPlainTextEdit con altura fija en líneas y señal de cambio de texto (equivalente a <textarea rows=N>).
class TextArea : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TextArea(int rows, QWidget* parent = nullptr);
    void setTextSilently(const QString& text);

    /// Añade un botón (y una entrada en el menú contextual) que abre el campo en el editor con formato
    /// de Jira (`MarkupEditorDialog`), con `title` como título de la ventana.
    void enableMarkupEditor(const QString& title);
    /// Abre el editor con formato; si se acepta con otro texto, lo sustituye y emite `edited`.
    void openMarkupEditor();

signals:
    void edited(const QString& text);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void placeMarkupButton();

    bool m_silent = false;
    QString m_markupTitle;
    QPushButton* m_markupButton = nullptr;
};

} // namespace qaflow
